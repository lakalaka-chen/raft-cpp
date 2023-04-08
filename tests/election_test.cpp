#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "raft.h"

using namespace raft;


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

bool checkTermsSame(const std::vector<RaftPtr> & machines) {
    std::set<int> terms;
    std::pair<int, bool> state;
    for (RaftPtr ptr: machines) {
        state = ptr->GetState();
        terms.insert(state.first);
    }
    return terms.size() == 1;
}


class ElectionTest: public testing::Test {
public:
    ElectionTest() = default;
    void StartUp(int n_servers) {
        n_servers_ = n_servers;
        rafts_.resize(n_servers);
        std::vector<PeerInfo> peers_info(n_servers_);
        std::vector<std::string> peers_name(n_servers_);

        for (int i = 0; i < n_servers_; i++) {
            peers_info[i] = {"127.0.0.1", tcp::RandomPort()};
            peers_name[i] = std::to_string(i + 1);
        }

        for (int i = 0; i < n_servers; i++) {
            rafts_[i] = Raft::Make(
                    peers_info, peers_name,
                    peers_name[i], peers_info[i].port);
            rafts_[i]->SetUp();
        }

        for (int i = 0; i < n_servers; i ++) {
            rafts_[i]->ConnectTo();
            rafts_[i]->StartTimers();
        }


        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        ASSERT_EQ(true, checkOneLeader(rafts_).first);
        ASSERT_EQ(true, checkTermsSame(rafts_));
    }
    int n_servers_;
    std::vector<RaftPtr> rafts_;
};


/*
 * 无故障的情况下可以选出唯一的领导者
 * 并且term最终保持一致
 * */
TEST_F(ElectionTest, InitialElection) {

    spdlog::set_level(spdlog::level::debug);
    StartUp(5);
}


/*
 * 1. 测试Leader掉线能不能选出新的领导者
 * 2. 测试超过半数结点掉线是不是无法选出领导者
 * */
TEST_F(ElectionTest, LeaderFailureTest) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(3);
    std::pair<bool, RaftPtr> check_result = checkOneLeader(rafts_);
    RaftPtr node_1 = check_result.second;
    ASSERT_EQ(check_result.first, true);
    node_1->Kill(); // 下线
    spdlog::debug("结点[{}]下线\n\n\n\n", node_1->GetName());
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    check_result = checkOneLeader(rafts_);
    RaftPtr node_2 = check_result.second;
    ASSERT_EQ(check_result.first, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    node_1->Recover(); // 重现上线
    spdlog::debug("结点[{}]重新上线\n\n\n\n", node_1->GetName());
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto status_1 = node_1->GetState();
    auto status_2 = node_2->GetState();
    ASSERT_EQ(status_1.second, false); // 旧领导者变成Follower
    ASSERT_EQ(status_2.second, true);  // 新领导者保持不变
    ASSERT_EQ(checkTermsSame(rafts_), true);

    node_1->Kill();
    node_2->Kill();
    spdlog::debug("结点[{}]下线", node_1->GetName());
    spdlog::debug("结点[{}]下线\n\n\n\n", node_2->GetName());
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, false);
    ASSERT_EQ(check_result.second, nullptr);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    node_1->Recover();
    node_2->Recover();
    spdlog::debug("结点[{}]重新上线", node_1->GetName());
    spdlog::debug("结点[{}]重新上线\n\n\n\n", node_2->GetName());
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);
    RaftPtr node_3 = check_result.second;
    status_1 = node_1->GetState();
    status_2 = node_2->GetState();
    auto status_3 = node_3->GetState();
    ASSERT_EQ(status_1.second, false);
    ASSERT_EQ(status_2.second, false);
    ASSERT_EQ(status_3.second, true);
    ASSERT_EQ(checkTermsSame(rafts_), true);
}


