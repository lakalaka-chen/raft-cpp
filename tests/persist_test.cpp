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


TEST_F(PersistTest, MorePersist) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(5);

    int index = 1;
    int n_iter = 5;
    std::pair<bool, RaftPtr> check_result;
    RaftPtr follower_1, follower_2, follower_3, follower_4;
    for (int iter = 0; iter < n_iter; iter ++) {
        ASSERT_NE(-1, one(rafts_, std::to_string(10+index), n_servers_));
        index ++;

        check_result = checkOneLeader(rafts_);
        ASSERT_EQ(check_result.first, true);
        ASSERT_NE(check_result.second, nullptr);

        follower_1 = killOneFollower(rafts_);
        follower_2 = killOneFollower(rafts_);
        spdlog::debug("结点[{},{}]下线\n\n\n\n", follower_1->GetName(), follower_2->GetName());

        ASSERT_NE(-1, one(rafts_, std::to_string(10+index), n_servers_-2));
        index ++;

        check_result.second->Kill();
        follower_3 = killOneFollower(rafts_);
        follower_4 = killOneFollower(rafts_);
        spdlog::debug("结点[{},{},{}]下线\n\n\n\n", check_result.second->GetName(), follower_3->GetName(), follower_4->GetName());

        int idx_1 = -1;
        for (int i = 0; i < n_servers_; i ++) {
            if (rafts_[i]->GetName() == follower_1->GetName()) {
                idx_1 = i;
                break;
            }
        }
        ASSERT_NE(idx_1, -1);
        crashAndSetup(rafts_, idx_1);

        int idx_2 = -1;
        for (int i = 0; i < n_servers_; i ++) {
            if (rafts_[i]->GetName() == follower_2->GetName()) {
                idx_2 = i;
                break;
            }
        }
        ASSERT_NE(idx_2, -1);
        crashAndSetup(rafts_, idx_2);


        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        int idx_3 = -1;
        for (int i = 0; i < n_servers_; i ++) {
            if (rafts_[i]->GetName() == follower_3->GetName()) {
                idx_3 = i;
                break;
            }
        }
        ASSERT_NE(idx_3, -1);
        crashAndSetup(rafts_, idx_3);

        rafts_[idx_1]->ConnectTo();
        rafts_[idx_2]->ConnectTo();
        rafts_[idx_3]->ConnectTo();
        rafts_[idx_1]->StartTimers();
        rafts_[idx_2]->StartTimers();
        rafts_[idx_3]->StartTimers();

        spdlog::debug("结点[{},{},{}]重置后再上线\n\n\n\n", follower_1->GetName(), follower_2->GetName(), follower_3->GetName());


        ASSERT_NE(-1, one(rafts_, std::to_string(10+index), n_servers_-2));
        index ++;

        follower_4->Recover();
        check_result.second->Recover();
        spdlog::debug("结点[{},{}]上线\n\n\n\n", follower_4->GetName(), check_result.second->GetName());

    }

    ASSERT_NE(-1, one(rafts_, "1000", n_servers_));

}


TEST_F(PersistTest, PartitionedLeaderAndOneFollowerCrash) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(3);

    ASSERT_NE(-1, one(rafts_, "101", n_servers_));

    std::pair<bool, RaftPtr> check_result = checkOneLeader(rafts_);
    ASSERT_EQ(true, check_result.first);
    ASSERT_NE(nullptr, check_result.second);
    RaftPtr leader = check_result.second;

    RaftPtr follower_2 = killOneFollower(rafts_);
    spdlog::debug("结点[{}]下线\n\n\n\n", follower_2->GetName());

    ASSERT_NE(-1, one(rafts_, "102", n_servers_-1));

    Raft::Crash(leader);
    RaftPtr follower_1 = killOneFollower(rafts_);
    Raft::Crash(follower_1);
    spdlog::debug("结点[{},{}]被Crash\n\n\n\n", leader->GetName(), follower_1->GetName());

    follower_2->Recover();
    spdlog::debug("结点[{}]上线\n\n\n\n", follower_2->GetName());

    int leader_idx = -1;
    for (int i = 0; i < n_servers_; i ++) {
        if (leader->GetName() == rafts_[i]->GetName()) {
            leader_idx = i;
            break;
        }
    }
    ASSERT_NE(-1, leader_idx);
    crashRestart(rafts_, leader_idx);
    spdlog::debug("结点[{}]重置后上线\n\n\n\n", leader->GetName());

    ASSERT_NE(-1, one(rafts_, "103", n_servers_-1));

    int idx_1;
    for (int i = 0; i < n_servers_; i ++) {
        if (follower_1->GetName() == rafts_[i]->GetName()) {
            idx_1 = i;
            break;
        }
    }
    ASSERT_NE(-1, idx_1);
    crashRestart(rafts_, idx_1);
    spdlog::debug("结点[{}]重置后上线\n\n\n\n", follower_1->GetName());


    ASSERT_NE(-1, one(rafts_, "104", n_servers_));



}