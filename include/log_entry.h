#pragma once

#include "serializer.h"
#include <string>

namespace raft {

struct LogEntry {
    int index{};
    int term{};
    std::string command{};
    [[nodiscard]] std::string String() const;
    [[nodiscard]] bool UpToDate(const LogEntry &other) const;
    [[nodiscard]] bool UpToDateOrSame(const LogEntry &other) const;
    [[nodiscard]] bool EqualTo(const LogEntry &other) const;

    [[nodiscard]] std::string Serialization() const;

    void Encode(Serializer &serializer) const;
    bool Decode(Serializer &serializer);
};

[[maybe_unused]] static const LogEntry default_empty_log = LogEntry{0, -1, ""};




}