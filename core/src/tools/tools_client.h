#ifndef PIBOT_TOOLS_CLIENT_H
#define PIBOT_TOOLS_CLIENT_H

#include <cstdint>
#include <string>
#include <vector>

namespace tools {

struct CloneResult {
    bool ok = false;
    std::string error;
    std::string archive_name;
    std::vector<char> archive_bytes;
};

// Minimal HTTP client for the tools service REST API (X-API-Key protected).
// Used by the /gclone command to clone a git repository and download the
// resulting .zip archive. Each call opens a short-lived connection.
class ToolsClient {
public:
    // `baseUrl` like "http://tools:8084".
    ToolsClient(std::string baseUrl, std::string apiKey);

    // Requests are only sent when both a URL and an API key are configured;
    // without an API key the tools service rejects everything with 401.
    bool enabled() const { return !baseUrl_.empty() && !apiKey_.empty(); }

    // Clones `url` and downloads the resulting archive. On success `archive_bytes`
    // holds the raw .zip data and `archive_name` a safe display file name.
    CloneResult clone(const std::string& url);

private:
    std::string baseUrl_;
    std::string apiKey_;
};

}  // namespace tools

#endif  // PIBOT_TOOLS_CLIENT_H
