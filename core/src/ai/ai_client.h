#ifndef PIBOT_AI_CLIENT_H
#define PIBOT_AI_CLIENT_H

#include <string>

namespace ai {

struct AskResult {
    bool ok = false;
    std::string response;
    std::string error;
};

// Minimal HTTP client for the AI service REST API (X-API-Key protected).
// Used by the /ai command to send a plain-text message and receive the plain
// text response. Each call opens a short-lived connection.
class AiClient {
public:
    // `baseUrl` like "http://ai:8082".
    AiClient(std::string baseUrl, std::string apiKey);

    // Requests are only sent when both a URL and an API key are configured;
    // without an API key the AI service rejects everything with 401.
    bool enabled() const { return !baseUrl_.empty() && !apiKey_.empty(); }

    // Sends `message` to POST /ai/ask and returns the AI response text.
    AskResult ask(const std::string& message);

private:
    std::string baseUrl_;
    std::string apiKey_;
};

}  // namespace ai

#endif  // PIBOT_AI_CLIENT_H
