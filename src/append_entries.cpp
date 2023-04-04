
#include "append_entries.h"
#include <sstream>

namespace raft {

std::string AppendEntriesArgs::String() const {
    std::ostringstream os;
    os << "term: " << term << "; ";
    os << "leader name: " << leader_name << "; ";
    os << "leader committed index: " << leader_committed_index << "; ";
    os << "log entries: ";
    for (const LogEntry & log: (*logs_ptr)) {
        os << "\n\t\t" << log.String();
    }
    return os.str();
}


std::string AppendEntriesArgs::Serialization() const {
    std::ostringstream os;
    os << "AppendEntries ";
    os << term << " ";
    os << leader_name << " ";
    os << leader_committed_index << " ";
    int n_logs = int(logs_ptr->size());
    os << n_logs-send_start_index << " ";
    for (int i = send_start_index; i < n_logs; i ++) {
        os << (*logs_ptr)[i].Serialization();
    }
    return os.str();
}


//  AppendEntriesReply
//      std::string server_name;
//      int term;
//      bool success;
//      int finished_index;
//      int conflict_index;


std::string AppendEntriesReply::String() const {
    std::ostringstream os;
    os << "server name: " << server_name << "; ";
    os << "term: " << term << "; ";
    os << "success: " << (success ? "True" : "False");
    os << "finished index: " << finished_index << "; ";
    os << "conflict index: " << conflict_index << " ";
    return os.str();
}


std::string AppendEntriesReply::Serialization() const {
    std::ostringstream os;
    os << "AppendEntriesReply ";
    os << server_name << " ";
    os << term << " ";
    os << success << " ";
    os << finished_index << " ";
    os << conflict_index << " ";
    return os.str();
}


}