#ifndef PIBOT_TOOLS_GIT_CLONER_H
#define PIBOT_TOOLS_GIT_CLONER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace tools {

struct CloneResult {
    bool ok = false;
    std::string error;
    std::string archive_path;
    std::string archive_name;
};

// Clones a git repository over HTTP(S), packs its working tree into a .zip
// archive and keeps the archive on disk for a short TTL. A repeated request
// for the same URL within the TTL reuses the cached archive instead of
// re-cloning; expired archives are deleted by the background sweeper to free
// disk space.
//
// Thread-safe: requests for the same URL are serialized so the same repo is
// never cloned twice at once; different URLs run in parallel.
class GitCloner {
public:
    GitCloner(std::string workdir, uint64_t max_bytes, uint64_t ttl_seconds);

    // Clones (or reuses the cached copy of) `url` and returns the path of the
    // freshly written .zip archive. `archive_name` is a safe display name for
    // the archive (e.g. "repo.zip").
    CloneResult clone(const std::string& url);

    // Deletes archives whose TTL has expired. Called periodically by the
    // background sweeper thread.
    void sweepExpired();

private:
    std::shared_ptr<std::mutex> urlMutex(const std::string& key);

    std::string workdir_;
    uint64_t max_bytes_;
    uint64_t ttl_seconds_;

    struct CacheEntry {
        std::string archive_path;
        int64_t last_used_at;
    };

    std::mutex cache_mutex_;
    std::unordered_map<std::string, CacheEntry> cache_;

    std::mutex url_mutexes_mutex_;
    std::unordered_map<std::string, std::shared_ptr<std::mutex>> url_mutexes_;
};

}  // namespace tools

#endif  // PIBOT_TOOLS_GIT_CLONER_H
