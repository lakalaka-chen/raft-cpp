#include "raft_test_common.h"
#include "spdlog/spdlog.h"
#include <set>

namespace raft {


std::pair<bool, RaftPtr>
checkOneLeader(const std::vector<RaftPtr> & machines) {
    int n_leader = 0;
    std::pair<int, bool> state;
    RaftPtr leader_ptr;
    for (RaftPtr ptr: machines) {
        if (ptr->Killed()) {
            continue;
        }
        state = ptr->GetState();
        if (state.second) {
            n_leader ++;
            leader_ptr = ptr;
        }
    }
    return {n_leader == 1, leader_ptr};
}

RaftPtr
killOneFollower(const std::vector<RaftPtr> & machines) {
    std::pair<int, bool> state;
    for (RaftPtr ptr: machines) {
        if (ptr->Killed()) {
            continue;
        }
        state = ptr->GetState();
        if (!state.second) {
            ptr->Kill();
            return ptr;
        }
    }
    return nullptr;
}


bool checkTermsSame(const std::vector<RaftPtr> & machines) {
    std::set<int> terms;
    std::pair<int, bool> state;
    for (RaftPtr ptr: machines) {
        state = ptr->GetState();
        terms.insert(state.first);
    }
    return terms.size() == 1;
}

/// 多少个服务器认为index位置的log_entry已经committed
std::pair<int, std::string> nCommitted(const std::vector<RaftPtr> & machines, int index) {
    int count = 0;
    std::string command;
    std::pair<bool, std::string> check_result;
    for (RaftPtr ptr: machines) {
        check_result = ptr->IsCommitted(index);
        if (check_result.first) {
            if (count > 0 && command != check_result.second) {
                spdlog::error("[{}]位置的日志没有达成一致, [{}]!=[{}]", index, command, check_result.second);
                return {0, ""};
            }
            count += 1;
            command = check_result.second;
        }
    }
    return {count, command};
}



int
one(const std::vector<RaftPtr> & machines, const std::string &command, int expectedServers) {
    auto t0 = Clock::now();
    std::tuple<int,int,bool> start_result;
    std::pair<int, std::string> commit_result;
    int n_committed;
    while (CalcDuration(Clock::now(), t0) < 10000) {
        int index = -1;
        for (RaftPtr ptr: machines) {
            if (ptr->Killed()) {
                continue;
            }
            spdlog::debug("[One] 尝试在结点[{}]上发起命令[{}]", ptr->GetName(), command);
            start_result = ptr->Start(command);
            if (std::get<2>(start_result)) {
                index = std::get<0>(start_result);
                break;
            }
        }

        if (index != -1) {
            spdlog::debug("客户端得到Leader回复, command[{}]已经被存入领导者日志列表中[{}]的位置", command, index);
            auto t1 = Clock::now();
            while (CalcDuration(Clock::now(), t1) < 2000) {
                commit_result = nCommitted(machines, index);
                spdlog::debug("有[{}]个结点完成command[{}]的日志复制", commit_result.first, command);
                if (commit_result.first > 0 && commit_result.first >= expectedServers) {
                    if (commit_result.second == command) {
                        spdlog::debug("已经超过[{}]个结点完成command[{}]的日志复制, 即将退出本次客户端请求", expectedServers, command);
                        return index;
                    }
                }
                std::this_thread::sleep_for(DurationUnit(20));
            }
        } else {
            std::this_thread::sleep_for(DurationUnit(50));
        }
    }
    spdlog::error("发起一个写请求[{}], 期待[{}]个以上结点提交, 但是过了10秒还是没有达成一致", command, expectedServers);
    return -1;
}



std::string configWait(const std::vector<RaftPtr> & machines, int index, int expectServers, int term) {

    int to = 10;
    for (int iter = 0; iter < 30; iter ++) {
        auto commit_result = nCommitted(machines, index);
        if (commit_result.first >= expectServers) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(to));
        if (to < 1000) {
            to *= 2;
        }
//        if (term > -1) {
//            for (RaftPtr ptr: machines) {
//                auto raft_status = ptr->GetState();
//                if (raft_status.first > term) {
//                    return "-1";
//                }
//            }
//        }
    }
    auto commit_result = nCommitted(machines, index);
    if (commit_result.first < expectServers) {
        spdlog::error("[ConfigWait] 仅仅有[{}]个结点提交{}号日志, 我们期待有[{}]个结点完成提交", commit_result.first, index, expectServers);
    }
    return commit_result.second;
}


}