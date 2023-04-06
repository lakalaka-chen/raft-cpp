
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
    os << prev_log_index << " ";
    os << prev_log_term << " ";
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

bool AppendEntriesReply::UnSerialization(const std::string &recv_msg, AppendEntriesReply &reply) {
    std::istringstream is(recv_msg);
    std::string method;
    is >> method;
    if (method != "AppendEntriesReply") {
        return false;
    }
    is >> reply.server_name;
    is >> reply.term;
    is >> reply.success;
    is >> reply.finished_index;
    is >> reply.conflict_index;
    return true;
}


}