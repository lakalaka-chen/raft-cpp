
#include <string>
#include <vector>

#include "log_entry.h"

namespace raft {

struct AppendEntriesArgs {
    int term{};
    std::string leader_name;
    int leader_committed_index{};
    int prev_log_term{};
    int prev_log_index{};
    const std::vector<LogEntry> *logs_ptr;
    int send_start_index{};
    explicit AppendEntriesArgs(const std::vector<LogEntry> * ptr, int start_index) {
        logs_ptr = ptr;
        send_start_index = start_index;
    }
    [[nodiscard]] std::string String() const;
    [[nodiscard]] std::string Serialization() const;
};

struct AppendEntriesReply {
    std::string server_name;
    int term;
    bool success;
    int finished_index;
    int conflict_index;
    [[nodiscard]] std::string String() const;
    [[nodiscard]] std::string Serialization() const;
};




}