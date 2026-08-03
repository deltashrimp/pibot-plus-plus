#include "storage/redis_storage.h"

#include <chrono>
#include <exception>
#include <iterator>
#include <utility>
#include <vector>

#include "logging/logger.h"

namespace {

const char* kKeyPrefix = "rp:commands:";

sw::redis::ConnectionOptions makeConnectionOptions(const std::string& host, uint16_t port) {
    sw::redis::ConnectionOptions opts;
    opts.host = host;
    opts.port = port;
    opts.connect_timeout = std::chrono::milliseconds(2000);
    opts.socket_timeout = std::chrono::milliseconds(2000);
    return opts;
}

sw::redis::ConnectionPoolOptions makePoolOptions(size_t size) {
    sw::redis::ConnectionPoolOptions opts;
    opts.size = size;
    opts.wait_timeout = std::chrono::milliseconds(100);
    opts.connection_lifetime = std::chrono::minutes(5);
    return opts;
}

std::string redisError(const sw::redis::Error& e) {
    return std::string(e.what());
}

}  // namespace

namespace rp {

RedisStorage::RedisStorage(std::string host, uint16_t port, size_t poolSize)
    : host_(std::move(host)),
      port_(port),
      redis_(makeConnectionOptions(host_, port_), makePoolOptions(poolSize)) {}

std::string RedisStorage::keyFor(int64_t chatId) const {
    return std::string(kKeyPrefix) + std::to_string(chatId);
}

bool RedisStorage::ping() {
    try {
        redis_.ping();
        return true;
    } catch (const sw::redis::Error& e) {
        Logger::error("redis ping failed: " + redisError(e));
        return false;
    }
}

std::optional<std::string> RedisStorage::getCommand(int64_t chatId,
                                                    const std::string& trigger) {
    try {
        auto value = redis_.hget(keyFor(chatId), trigger);
        if (value) {
            return std::optional<std::string>(*value);
        }
    } catch (const sw::redis::Error& e) {
        Logger::error("redis hget failed: " + redisError(e));
    }
    return std::nullopt;
}

bool RedisStorage::hasCommand(int64_t chatId, const std::string& trigger) {
    try {
        return redis_.hexists(keyFor(chatId), trigger);
    } catch (const sw::redis::Error& e) {
        Logger::error("redis hexists failed: " + redisError(e));
        return false;
    }
}

bool RedisStorage::addCommand(int64_t chatId, const std::string& trigger,
                              const std::string& response) {
    try {
        return redis_.hset(keyFor(chatId), trigger, response) == 1;
    } catch (const sw::redis::Error& e) {
        Logger::error("redis hset failed: " + redisError(e));
        return false;
    }
}

bool RedisStorage::addIfAbsent(int64_t chatId, const std::string& trigger,
                               const std::string& response) {
    try {
        return redis_.hsetnx(keyFor(chatId), trigger, response) != 0;
    } catch (const sw::redis::Error& e) {
        Logger::error("redis hsetnx failed: " + redisError(e));
        return false;
    }
}

bool RedisStorage::updateCommand(int64_t chatId, const std::string& trigger,
                                 const std::string& response) {
    try {
        redis_.hset(keyFor(chatId), trigger, response);
        return true;
    } catch (const sw::redis::Error& e) {
        Logger::error("redis hset failed: " + redisError(e));
        return false;
    }
}

bool RedisStorage::removeCommand(int64_t chatId, const std::string& trigger) {
    try {
        return redis_.hdel(keyFor(chatId), trigger) == 1;
    } catch (const sw::redis::Error& e) {
        Logger::error("redis hdel failed: " + redisError(e));
        return false;
    }
}

std::unordered_map<std::string, std::string> RedisStorage::listCommands(int64_t chatId) {
    std::unordered_map<std::string, std::string> result;
    try {
        std::vector<std::pair<std::string, sw::redis::OptionalString>> entries;
        redis_.hgetall(keyFor(chatId), std::back_inserter(entries));
        for (const auto& entry : entries) {
            result[entry.first] = entry.second ? *entry.second : "";
        }
    } catch (const sw::redis::Error& e) {
        Logger::error("redis hgetall failed: " + redisError(e));
    }
    return result;
}

size_t RedisStorage::countCommands(int64_t chatId) {
    try {
        return static_cast<size_t>(redis_.hlen(keyFor(chatId)));
    } catch (const sw::redis::Error& e) {
        Logger::error("redis hlen failed: " + redisError(e));
        return 0;
    }
}

}  // namespace rp
