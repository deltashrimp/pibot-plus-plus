#ifndef PIBOT_CORE_CLIENT_HPP
#define PIBOT_CORE_CLIENT_HPP

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

// Minimal HTTP client for Core's internal REST API (X-API-Key protected).
//
// Used to fetch a user's rank in a chat so that privileged users (devs,
// owners, admins) are exempt from automatic moderation, mirroring the old
// Python bot's behaviour.
//
// Thread-safe: may be called concurrently from multiple threads. Successful
// results are cached briefly (see kCacheTtlSeconds); failed lookups are never
// cached so a transient Core outage is retried on the next call.
class CoreClient {
public:
    CoreClient(std::string host, uint16_t port, std::string api_key);

    // Rank checks require a configured API key: without one Core rejects all
    // requests with 401, so the check would be pointless.
    bool enabled() const { return !api_key_.empty(); }

    // Rank of `user_id` in `chat_id`, per Core semantics:
    //   0 dev, 1 owner, 2 admin+, 3 admin, 4 member (default when absent).
    // Returns -1 when Core could not be reached or answered unexpectedly.
    int getChatRank(int64_t chat_id, int64_t user_id);

private:
    struct CacheEntry {
        int rank;
        double fetched_at;
    };

    std::string host_;
    uint16_t port_;
    std::string api_key_;

    static constexpr double kCacheTtlSeconds = 60.0;

    std::mutex cache_mutex_;
    std::unordered_map<int64_t, std::unordered_map<int64_t, CacheEntry>> cache_;
};

#endif  // PIBOT_CORE_CLIENT_HPP
