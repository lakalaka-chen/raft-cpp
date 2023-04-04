
#include "log_entry.h"
#include <string>
#include <sstream>

namespace raft {

std::string LogEntry::String() const {
    std::string s;
    s += "'index: " + std::to_string(index);
    s += "; term: " + std::to_string(term);
    s += "; cmd: " + command;
    s += "'";
    return s;
}

bool LogEntry::UpToDate(const LogEntry &other) const {
    if (term > other.term) {
        return true;
    } else if (term < other.term) {
        return false;
    }
    return index > other.index;
}

bool LogEntry::UpToDateOrSame(const LogEntry &other) const {
    if (term > other.term) {
        return true;
    } else if (term < other.term) {
        return false;
    }
    return index >= other.index;
}


bool LogEntry::EqualTo(const LogEntry &other) const {
    return term == other.term && index == other.index;
}


std::string LogEntry::Serialization() const {
    std::ostringstream os;
    os << index << " ";
    os << term << " ";
    os << command << " ";
    return os.str();
}



}