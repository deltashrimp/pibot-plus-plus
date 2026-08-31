#ifndef PIBOT_CONFIG_H
#define PIBOT_CONFIG_H

#include <string>
#include <unordered_map>

#include <toml++/toml.hpp>

struct AutomodConfig {
    double spam_window_seconds = 3.0;
    size_t spam_limit = 9;
    int mute_duration_seconds = 60;
    int privileged_max_rank = 3;
    double cache_ttl_seconds = 60.0;
    size_t tracked_cap = 4096;
};

struct CommandsConfig {
    std::unordered_map<std::string, int> required_ranks;

    int getRank(const std::string& command, int fallback = 4) const {
        auto it = required_ranks.find(command);
        return it != required_ranks.end() ? it->second : fallback;
    }
};

struct AppConfig {
    AutomodConfig automod;
    CommandsConfig commands;
};

inline AppConfig loadConfig(const std::string& path) {
    AppConfig config;
    try {
        toml::table tbl = toml::parse_file(path);

        if (auto automod = tbl["automod"].as_table()) {
            if (auto v = automod->get_as<double>("spam_window_seconds"))
                config.automod.spam_window_seconds = v->get();
            if (auto v = automod->get_as<int64_t>("spam_limit"))
                config.automod.spam_limit = static_cast<size_t>(v->get());
            if (auto v = automod->get_as<int64_t>("mute_duration_seconds"))
                config.automod.mute_duration_seconds = static_cast<int>(v->get());
            if (auto v = automod->get_as<int64_t>("privileged_max_rank"))
                config.automod.privileged_max_rank = static_cast<int>(v->get());
            if (auto v = automod->get_as<double>("cache_ttl_seconds"))
                config.automod.cache_ttl_seconds = v->get();
            if (auto v = automod->get_as<int64_t>("tracked_cap"))
                config.automod.tracked_cap = static_cast<size_t>(v->get());
        }

        if (auto commands = tbl["commands"].as_table()) {
            for (auto& [key, value] : *commands) {
                if (auto val = value.as_integer()) {
                    config.commands.required_ranks[std::string(key.str())] =
                        static_cast<int>(val->get());
                }
            }
        }
    } catch (const toml::parse_error&) {
    }
    return config;
}

#endif  // PIBOT_CONFIG_H
