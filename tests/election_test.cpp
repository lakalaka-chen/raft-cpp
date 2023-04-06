#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "raft.h"

using namespace raft;


bool checkOneLeader(const std::vector<RaftPtr> & machines) {
    int n_leader = 0;
    std::pair<int, bool> state;
    for (RaftPtr ptr: machines) {
        state = ptr->GetState();
        if (state.second) {
            n_leader ++;
        }
    }
    return n_leader == 1;
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


void singleInitialElection(int n_servers) {
    std::vector<PeerInfo> peers_info(n_servers);
    std::vector<std::string> peers_name(n_servers);

    for (int i = 0; i < n_servers; i++) {
        peers_info[i] = {"127.0.0.1", tcp::RandomPort()};
        peers_name[i] = std::to_string(i + 1);
    }

    std::vector<RaftPtr> raft_pointers(n_servers);

    for (int i = 0; i < n_servers; i++) {
        raft_pointers[i] = Raft::Make(
                peers_info, peers_name,
                peers_name[i], peers_info[i].port);
        raft_pointers[i]->SetUp();
    }

    for (int i = 0; i < n_servers; i ++) {
        raft_pointers[i]->ConnectTo();
        raft_pointers[i]->StartTimers();
    }


    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    ASSERT_EQ(true, checkOneLeader(raft_pointers));
    ASSERT_EQ(true, checkTermsSame(raft_pointers));
}

/*
 * 无故障的情况下可以选出唯一的领导者
 * 并且term最终保持一致
 * */
TEST(ElectionTest, InitialElection) {

    spdlog::set_level(spdlog::level::debug);
//    singleInitialElection(3);
//    singleInitialElection(5);
    singleInitialElection(7);
}



