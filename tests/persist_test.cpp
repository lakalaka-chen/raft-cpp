#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "raft_test_common.h"
#include "wait_group/wait_group.h"

using namespace raft;


class PersistTest: public testing::Test {
public:
    PersistTest() = default;
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
    int n_servers_{0};
    std::vector<RaftPtr> rafts_;
};


TEST_F(PersistTest, Crash) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(3);
    auto check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);
    ASSERT_NE(check_result.second, nullptr);

    int idx = -1;
    for (int i = 0; i < n_servers_; i ++) {
        if (rafts_[i]->GetName() == check_result.second->GetName()) {
            idx = i;
            break;
        }
    }

    ASSERT_NE(-1, idx);
    crashAndSetup(rafts_, idx);
    rafts_[idx]->ConnectTo();
    rafts_[idx]->StartTimers();


    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);

    ASSERT_EQ(true, checkTermsSame(rafts_));

}


TEST_F(PersistTest, BasicPersist) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(3);
    auto check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);
    ASSERT_NE(check_result.second, nullptr);

    ASSERT_NE(-1, one(rafts_, "11", n_servers_));

    for (int i = 0; i < n_servers_; i ++) {
        crashAndSetup(rafts_, i);
    }
    for (int i = 0; i < n_servers_; i ++) {
        rafts_[i]->ConnectTo();
        rafts_[i]->StartTimers();
    }

    for (int i = 0; i < n_servers_; i ++) {
        rafts_[i]->Kill();
        rafts_[i]->Recover();
    }

    ASSERT_NE(-1, one(rafts_, "12", n_servers_));
    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);
    ASSERT_NE(check_result.second, nullptr);
    RaftPtr leader1 = check_result.second;

    int idx = -1;
    for (int i = 0; i < n_servers_; i ++) {
        if (rafts_[i]->GetName() == leader1->GetName()) {
            idx = i;
            break;
        }
    }
    ASSERT_NE(-1, idx);
    crashRestart(rafts_, idx);

    ASSERT_NE(-1, one(rafts_, "13", n_servers_-1));


    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);
    ASSERT_NE(check_result.second, nullptr);
    RaftPtr leader2 = check_result.second;

    ASSERT_EQ(true, checkTermsSame(rafts_));

    leader2->Kill();

    ASSERT_NE(-1, one(rafts_, "14", n_servers_-1));

    idx = -1;
    for (int i = 0; i < n_servers_; i ++) {
        if (rafts_[i]->GetName() == leader2->GetName()) {
            idx = i;
            break;
        }
    }
    ASSERT_NE(-1, idx);
    crashRestart(rafts_, idx);

    configWait(rafts_, 4, n_servers_, -1);

    RaftPtr follower = killOneFollower(rafts_);
    follower->Kill();
    ASSERT_NE(-1, one(rafts_, "15", n_servers_-1));

    idx = -1;
    for (int i = 0; i < n_servers_; i ++) {
        if (rafts_[i]->GetName() == follower->GetName()) {
            idx = i;
            break;
        }
    }
    ASSERT_NE(-1, idx);
    crashRestart(rafts_, idx);

    ASSERT_NE(-1, one(rafts_, "16", n_servers_-1));

    spdlog::info("... Passed\n");
}