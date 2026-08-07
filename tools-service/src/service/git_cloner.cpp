#include "service/git_cloner.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "logging/logger.h"

namespace {

int64_t unixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// FNV-1a 64-bit hash; used to derive a collision-free, filesystem-safe file
// name from an arbitrary repo URL.
uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

// Runs a shell command and captures its combined stdout/stderr. Returns true
// only when the command exited with status 0.
bool runCommand(const std::string& command, std::string& output) {
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return false;
    }
    char buffer[4096];
    size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        output.append(buffer, n);
    }
    const int status = pclose(pipe);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

uint64_t fileSize(const std::string& path) {
    struct stat st {};
    if (stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}

bool removePath(const std::string& path) {
    std::string output;
    return runCommand("rm -rf -- " + shellQuote(path), output);
}

// "https://github.com/user/repo.git" or "git@github.com:user/repo.git" -> "repo".
std::string repoName(const std::string& url) {
    std::string name = url;
    while (!name.empty() && name.back() == '/') {
        name.pop_back();
    }
    const size_t slash = name.find_last_of("/:");
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".git") == 0) {
        name.resize(name.size() - 4);
    }
    if (name.empty()) {
        name = "repo";
    }
    return name;
}

std::string sanitizeFilename(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-') {
            out += static_cast<char>(c);
        } else {
            out += '_';
        }
    }
    return out;
}

bool looksLikeRepoUrl(const std::string& url) {
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 ||
           url.rfind("git://", 0) == 0 || url.rfind("git@", 0) == 0;
}

}  // namespace

namespace tools {

GitCloner::GitCloner(std::string workdir, uint64_t max_bytes, uint64_t ttl_seconds)
    : workdir_(std::move(workdir)),
      max_bytes_(max_bytes),
      ttl_seconds_(ttl_seconds) {}

std::shared_ptr<std::mutex> GitCloner::urlMutex(const std::string& key) {
    std::lock_guard<std::mutex> lock(url_mutexes_mutex_);
    auto it = url_mutexes_.find(key);
    if (it != url_mutexes_.end()) {
        return it->second;
    }
    auto m = std::make_shared<std::mutex>();
    url_mutexes_[key] = m;
    return m;
}

CloneResult GitCloner::clone(const std::string& rawUrl) {
    const std::string url = trim(rawUrl);
    CloneResult result;
    if (url.empty()) {
        result.error = "URL репозитория не указан.";
        return result;
    }
    if (!looksLikeRepoUrl(url)) {
        result.error = "Не поддерживаемый URL. Ожидается http(s):// или git@ URL.";
        return result;
    }

    const std::string key = url;
    auto urlLock = urlMutex(key);
    std::lock_guard<std::mutex> urlGuard(*urlLock);

    // Fast path: fresh cache entry -> serve without re-cloning. A hit also
    // refreshes the TTL, so an archive being served is never swept away mid
    // download and actively used repos stay cached.
    {
        std::lock_guard<std::mutex> guard(cache_mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            const int64_t now = unixNow();
            if (now - it->second.last_used_at < static_cast<int64_t>(ttl_seconds_)) {
                it->second.last_used_at = now;
                result.ok = true;
                result.archive_path = it->second.archive_path;
                result.archive_name = sanitizeFilename(repoName(url)) + ".zip";
                Logger::event("clone_cache_hit", url);
                return result;
            }
            const std::string stale = it->second.archive_path;
            cache_.erase(it);
            removePath(stale);
            Logger::event("clone_cache_expired", stale);
        }
    }

    const std::string hash = std::to_string(fnv1a(key));
    const std::string archivePath = workdir_ + "/" + hash + ".zip";
    const std::string cloneDir = workdir_ + "/" + hash + ".src";

    Logger::event("clone_start", url);

    // Leftovers from a previously failed attempt.
    removePath(cloneDir);
    removePath(archivePath);

    std::string output;
    std::string cmd = "git clone --depth 1 -q -- " + shellQuote(url) + " " + shellQuote(cloneDir);
    if (!runCommand(cmd, output)) {
        result.error = "Не удалось клонировать репозиторий.";
        const std::string detail = trim(output);
        if (!detail.empty()) {
            result.error += " " + detail.substr(0, 300);
        }
        removePath(cloneDir);
        Logger::warn("git clone failed for " + url + ": " + output);
        return result;
    }

    // The clone's .git directory is not part of the archive.
    removePath(cloneDir + "/.git");

    cmd = "cd " + shellQuote(cloneDir) + " && zip -q -r -y " + shellQuote(archivePath) + " .";
    if (!runCommand(cmd, output)) {
        result.error = "Не удалось создать архив.";
        removePath(cloneDir);
        removePath(archivePath);
        Logger::warn("zip failed for " + url + ": " + output);
        return result;
    }
    removePath(cloneDir);

    const uint64_t size = fileSize(archivePath);
    if (size == 0 || size > max_bytes_) {
        result.error = "Архив репозитория слишком большой (" +
                       std::to_string(size / 1024 / 1024) + " МБ). Лимит: " +
                       std::to_string(max_bytes_ / 1024 / 1024) + " МБ.";
        removePath(archivePath);
        Logger::warn("archive too big for " + url + ": " + std::to_string(size) + " bytes");
        return result;
    }

    {
        std::lock_guard<std::mutex> guard(cache_mutex_);
        cache_[key] = CacheEntry{archivePath, unixNow()};
    }

    Logger::event("clone_done", url + " -> " + archivePath);
    result.ok = true;
    result.archive_path = archivePath;
    result.archive_name = sanitizeFilename(repoName(url)) + ".zip";
    return result;
}

void GitCloner::sweepExpired() {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    const int64_t now = unixNow();
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (now - it->second.last_used_at >= static_cast<int64_t>(ttl_seconds_)) {
            Logger::event("clone_sweep", it->second.archive_path);
            removePath(it->second.archive_path);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace tools
