#ifndef PIBOT_AUTOMODERATOR_HPP
#define PIBOT_AUTOMODERATOR_HPP

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Actions returned by the moderator to the caller.
enum class ActionType {
    Allow,          // message passes, no action
    MuteTemporary,  // mute user for a given duration
    MutePermanent   // mute the user indefinitely
};

struct Action {
    ActionType type;
    int duration_seconds;  // only meaningful for MuteTemporary
    bool delete_message;   // true if the message should be deleted
};

// String form of an action type: "Allow", "MuteTemporary" or "MutePermanent".
// Shared by the moderator's structured logs and the HTTP API responses.
const char* action_type_to_string(ActionType type);

// Reusable, thread-safe auto-moderation module.
//
// Implements two independent checks, in order:
//   1. Keyword filter  – hard-coded forbidden phrases (case-insensitive
//      substring match). A hit yields MutePermanent + delete_message.
//   2. Anti-spam       – per (chat, user) message counts within a 3-second
//      sliding window. Sustained sending above 3 messages/second for 3 seconds
//      (more than 9 messages in the window) yields MuteTemporary(60s).
//
// All state lives in memory; nothing is persisted across restarts.
class AutoModerator {
public:
    AutoModerator() = default;

    // Process an incoming message.
    //   chat_id, user_id – identifiers (64-bit integers)
    //   text             – the message content (UTF-8 string)
    //   timestamp        – Unix timestamp in seconds (fractional part ok)
    //
    // Thread-safe: may be called concurrently from multiple threads.
    Action process_message(int64_t chat_id, int64_t user_id,
                           const std::string& text, double timestamp);

    // Emit a decision log and return Allow without counting the message toward
    // anti-spam. Used for privileged users (e.g. admins) that the caller
    // exempts from automatic moderation before calling process_message.
    Action skip_privileged(int64_t chat_id, int64_t user_id,
                           const std::string& text);

private:
    // Returns the (lower-case) forbidden phrase contained in `lower_text`,
    // or an empty string if none of the patterns match.
    std::string match_filter(const std::string& lower_text) const;

    // Drop spam-tracking entries that can no longer affect decisions.
    void prune_locked(double now);

    // spam_trackers_[chat_id][user_id] = timestamps of recent messages.
    // Invariant (guarded by mutex_): timestamps are within the spam window.
    std::unordered_map<int64_t, std::unordered_map<int64_t, std::vector<double>>>
        spam_trackers_;
    size_t tracked_count_ = 0;  // total timestamps held, for GC decisions

    std::mutex mutex_;
};

#endif  // PIBOT_AUTOMODERATOR_HPP
