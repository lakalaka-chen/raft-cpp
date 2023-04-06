#include "request_votes.h"
#include <sstream>

namespace raft {

//  RequestVoteArgs
//    int term;
//    std::string candidate_name;
//    int last_log_index;
//    int last_log_term;
std::string RequestVoteArgs::String() const {
    std::string str;
    str += "term: " + std::to_string(term);
    str += "; candidate: " + candidate_name;
    str += "; last log: { index=" + std::to_string(last_log_index);
    str += ", term=" + std::to_string(last_log_term) + " }";
    return str;
}

std::string RequestVoteArgs::Serialization() const {
    std::ostringstream os;
    os << "RequestVotes ";
    os << term << " ";
    os << candidate_name << " ";
    os << last_log_index << " ";
    os << last_log_term << " ";
    return os.str();
}


std::string RequestVoteReply::String() const {
    std::string str;
    str += "term: " + std::to_string(term);
    str += "; ok: " + std::string(vote_granted ? "True": "False");
    return str;
}


std::string RequestVoteReply::Serialization() const {
    std::ostringstream os;
    os << "RequestVoteReply ";
    os << term << " ";
    os << vote_granted << " ";
    os << err_msg << " ";
    return os.str();
}

bool RequestVoteReply::UnSerialization(const std::string & recv_msg, RequestVoteReply &reply) {
    std::istringstream is(recv_msg);
    std::string method;
    is >> method;
    if (method != "RequestVoteReply") {
        return false;
    }
    is >> reply.term;
    is >> reply.vote_granted;
    is >> reply.err_msg;
    return true;
}


}