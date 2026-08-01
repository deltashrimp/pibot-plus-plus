#ifndef PIBOT_DB_MANAGER_H
#define PIBOT_DB_MANAGER_H

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <pqxx/pqxx>

struct DbConfig {
    std::string host;
    std::string port;
    std::string dbname;
    std::string user;
    std::string password;
};

struct MuteInfo {
    bool muted = false;
    int64_t until = 0;
};

struct RankEntry {
    int64_t user_id;
    int rank;
};

class DbManager : public std::enable_shared_from_this<DbManager> {
public:
    static std::shared_ptr<DbManager> create(const DbConfig& config);

    ~DbManager();
    DbManager(const DbManager&) = delete;
    DbManager& operator=(const DbManager&) = delete;

    void initSchema();

    void addGlobalBan(int64_t userId);
    void removeGlobalBan(int64_t userId);
    bool isGloballyBanned(int64_t userId);
    std::vector<int64_t> getGlobalBans();

    bool ping();

    void setChatRank(int64_t chatId, int64_t userId, int rank);
    int getChatRank(int64_t chatId, int64_t userId);
    std::vector<RankEntry> getChatRanks(int64_t chatId);

    void addDev(int64_t userId);
    bool isDev(int64_t userId);

    void addMute(int64_t chatId, int64_t userId, int64_t until);
    void removeMute(int64_t chatId, int64_t userId);
    bool isMuted(int64_t chatId, int64_t userId);
    MuteInfo getMute(int64_t chatId, int64_t userId);

private:
    explicit DbManager(DbConfig config);

    void connect();
    void ensureConnected();
    void loadGlobalBansLocked();

    template <typename Func>
    auto withRetry(Func&& fn) -> decltype(fn()) {
        constexpr int kMaxAttempts = 5;
        for (int attempt = 1; attempt <= kMaxAttempts; ++attempt) {
            try {
                ensureConnected();
                return fn();
            } catch (const pqxx::broken_connection&) {
                if (attempt == kMaxAttempts) {
                    throw;
                }
                connect();
                std::this_thread::sleep_for(std::chrono::milliseconds(250 * attempt));
            }
        }
        throw std::runtime_error("db operation failed");
    }

    DbConfig config_;
    std::unique_ptr<pqxx::connection> conn_;
    std::mutex mutex_;
    std::unordered_set<int64_t> globalBansCache_;
};

#endif
