#include "database/db_manager.h"

#include <chrono>
#include <stdexcept>
#include <thread>

#include "logging/logger.h"

namespace {

std::string quoteConnInfoValue(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\\' || c == '\'') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string buildConnInfo(const DbConfig& config) {
    return "host=" + quoteConnInfoValue(config.host) +
           " port=" + quoteConnInfoValue(config.port) +
           " dbname=" + quoteConnInfoValue(config.dbname) +
           " user=" + quoteConnInfoValue(config.user) +
           " password=" + quoteConnInfoValue(config.password) +
           " connect_timeout=5";
}

}  // namespace

DbManager::DbManager(DbConfig config) : config_(std::move(config)) {}

DbManager::~DbManager() = default;

std::shared_ptr<DbManager> DbManager::create(const DbConfig& config) {
    auto db = std::shared_ptr<DbManager>(new DbManager(config));
    db->connect();
    return db;
}

void DbManager::connect() {
    try {
        conn_ = std::make_unique<pqxx::connection>(buildConnInfo(config_));
    } catch (const pqxx::broken_connection& e) {
        Logger::error(std::string("postgres connection failed: ") + e.what());
        throw;
    }
}

void DbManager::ensureConnected() {
    if (!conn_ || !conn_->is_open()) {
        connect();
    }
}

void DbManager::initSchema() {
    std::lock_guard<std::mutex> lock(mutex_);
    withRetry([this] {
        pqxx::work tx(*conn_);
        tx.exec0("CREATE TABLE IF NOT EXISTS global_bans ("
                 "user_id BIGINT PRIMARY KEY)");
        tx.exec0("CREATE TABLE IF NOT EXISTS chat_ranks ("
                 "chat_id BIGINT NOT NULL,"
                 "user_id BIGINT NOT NULL,"
                 "rank INT NOT NULL DEFAULT 4,"
                 "PRIMARY KEY (chat_id, user_id))");
        tx.exec0("CREATE TABLE IF NOT EXISTS chat_mutes ("
                 "chat_id BIGINT NOT NULL,"
                 "user_id BIGINT NOT NULL,"
                 "until BIGINT NOT NULL DEFAULT 0,"
                 "PRIMARY KEY (chat_id, user_id))");
        tx.exec0("CREATE TABLE IF NOT EXISTS bot_config ("
                 "key TEXT PRIMARY KEY,"
                 "value TEXT)");
        tx.exec0("CREATE TABLE IF NOT EXISTS devs ("
                 "user_id BIGINT PRIMARY KEY)");
        tx.exec_params0("INSERT INTO devs (user_id) VALUES (934151958) "
                        "ON CONFLICT (user_id) DO NOTHING");
        tx.commit();
    });
    loadGlobalBansLocked();
}

void DbManager::loadGlobalBansLocked() {
    withRetry([this] {
        pqxx::work tx(*conn_);
        pqxx::result result = tx.exec("SELECT user_id FROM global_bans");
        globalBansCache_.clear();
        for (const auto& row : result) {
            globalBansCache_.insert(row[0].as<int64_t>());
        }
        tx.commit();
    });
}

void DbManager::addGlobalBan(int64_t userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    withRetry([&] {
        pqxx::work tx(*conn_);
        tx.exec_params0("INSERT INTO global_bans (user_id) VALUES ($1) "
                        "ON CONFLICT (user_id) DO NOTHING",
                        userId);
        tx.commit();
    });
    globalBansCache_.insert(userId);
}

void DbManager::removeGlobalBan(int64_t userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    withRetry([&] {
        pqxx::work tx(*conn_);
        tx.exec_params0("DELETE FROM global_bans WHERE user_id = $1", userId);
        tx.commit();
    });
    globalBansCache_.erase(userId);
}

bool DbManager::isGloballyBanned(int64_t userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return globalBansCache_.count(userId) > 0;
}

std::vector<int64_t> DbManager::getGlobalBans() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int64_t> ids;
    ids.reserve(globalBansCache_.size());
    for (int64_t id : globalBansCache_) {
        ids.push_back(id);
    }
    return ids;
}

bool DbManager::ping() {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
        return withRetry([this] {
            pqxx::work tx(*conn_);
            tx.exec1("SELECT 1");
            tx.commit();
            return true;
        });
    } catch (const std::exception&) {
        return false;
    }
}

void DbManager::setChatRank(int64_t chatId, int64_t userId, int rank) {
    std::lock_guard<std::mutex> lock(mutex_);
    withRetry([&] {
        pqxx::work tx(*conn_);
        tx.exec_params0("INSERT INTO chat_ranks (chat_id, user_id, rank) VALUES ($1, $2, $3) "
                        "ON CONFLICT (chat_id, user_id) DO UPDATE SET rank = EXCLUDED.rank",
                        chatId, userId, rank);
        tx.commit();
    });
}

int DbManager::getChatRank(int64_t chatId, int64_t userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return withRetry([&] {
        pqxx::work tx(*conn_);
        pqxx::result devResult = tx.exec_params("SELECT 1 FROM devs WHERE user_id = $1", userId);
        if (!devResult.empty()) {
            return 0;
        }
        pqxx::result result =
            tx.exec_params("SELECT rank FROM chat_ranks WHERE chat_id = $1 AND user_id = $2",
                           chatId, userId);
        if (result.empty()) {
            return 4;
        }
        return result[0][0].as<int>();
    });
}

std::vector<RankEntry> DbManager::getChatRanks(int64_t chatId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return withRetry([&] {
        pqxx::work tx(*conn_);
        pqxx::result result = tx.exec_params(
            "SELECT user_id, rank FROM chat_ranks WHERE chat_id = $1 "
            "AND rank BETWEEN 1 AND 3 ORDER BY rank ASC, user_id ASC",
            chatId);
        std::vector<RankEntry> entries;
        entries.reserve(result.size());
        for (const auto& row : result) {
            entries.push_back({row[0].as<int64_t>(), row[1].as<int>()});
        }
        return entries;
    });
}

void DbManager::addDev(int64_t userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    withRetry([&] {
        pqxx::work tx(*conn_);
        tx.exec_params0("INSERT INTO devs (user_id) VALUES ($1) "
                        "ON CONFLICT (user_id) DO NOTHING",
                        userId);
        tx.commit();
    });
}

bool DbManager::isDev(int64_t userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return withRetry([&] {
        pqxx::work tx(*conn_);
        pqxx::result result = tx.exec_params("SELECT 1 FROM devs WHERE user_id = $1", userId);
        return !result.empty();
    });
}

void DbManager::addMute(int64_t chatId, int64_t userId, int64_t until) {
    std::lock_guard<std::mutex> lock(mutex_);
    withRetry([&] {
        pqxx::work tx(*conn_);
        tx.exec_params0("INSERT INTO chat_mutes (chat_id, user_id, until) VALUES ($1, $2, $3) "
                        "ON CONFLICT (chat_id, user_id) DO UPDATE SET until = EXCLUDED.until",
                        chatId, userId, until);
        tx.commit();
    });
}

void DbManager::removeMute(int64_t chatId, int64_t userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    withRetry([&] {
        pqxx::work tx(*conn_);
        tx.exec_params0("DELETE FROM chat_mutes WHERE chat_id = $1 AND user_id = $2",
                        chatId, userId);
        tx.commit();
    });
}

bool DbManager::isMuted(int64_t chatId, int64_t userId) {
    return getMute(chatId, userId).muted;
}

MuteInfo DbManager::getMute(int64_t chatId, int64_t userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return withRetry([&] {
        pqxx::work tx(*conn_);
        pqxx::result result =
            tx.exec_params("SELECT until FROM chat_mutes WHERE chat_id = $1 AND user_id = $2",
                           chatId, userId);
        MuteInfo info;
        if (result.empty()) {
            return info;
        }
        int64_t until = result[0][0].as<int64_t>();
        info.until = until;
        if (until == 0) {
            info.muted = true;
        } else {
            int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
            info.muted = until > now;
        }
        return info;
    });
}
