#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "raft_test_common.h"


using namespace raft;



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


TEST_F(ElectionTest, ReElection) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(3);

    spdlog::info("Test: election after network failure...\n");

    auto check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);
    RaftPtr leader1 = check_result.second;
    ASSERT_NE(leader1, nullptr);

    leader1->Kill();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);

    leader1->Recover();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);
    RaftPtr leader2 = check_result.second;
    ASSERT_NE(leader2, nullptr);

    leader2->Kill();
    RaftPtr follower = killOneFollower(rafts_);

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, false);

    follower->Recover();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);

    leader2->Recover();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);

    spdlog::info("... Passed\n");

}

