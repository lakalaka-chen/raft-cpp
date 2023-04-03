
#include <unistd.h>
#include <string>

namespace raft {

struct LogEntry {
    uint32_t index{};
    int32_t term{};
    std::string command{};
    [[nodiscard]] std::string String() const;
    [[nodiscard]] bool UpToDate(const LogEntry &other) const;
    [[nodiscard]] bool UpToDateOrSame(const LogEntry &other) const;
    [[nodiscard]] bool EqualTo(const LogEntry &other) const;
};

[[maybe_unused]] static const LogEntry default_empty_log = LogEntry{0, -1, ""};




}