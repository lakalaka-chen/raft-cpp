#pragma once

#include <string>

namespace raft {


struct RequestVoteArgs {
    int term;
    std::string candidate_name;
    int last_log_index;
    int last_log_term;
    [[nodiscard]] std::string String() const;
    [[nodiscard]] std::string Serialization() const;
};

struct RequestVoteReply {
    int term;
    bool vote_granted;
    std::string err_msg;  // TODO: 现在的err_msg不可以有空字符
    [[nodiscard]] std::string String() const;
    [[nodiscard]] std::string Serialization() const;

    static bool UnSerialization(const std::string & recv_msg, RequestVoteReply &reply);
};



}