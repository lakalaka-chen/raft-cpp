#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "raft_test_common.h"
#include "wait_group/wait_group.h"

using namespace raft;


class UnreliableTest: public testing::Test {
public:
    UnreliableTest() = default;
    void StartUp(int n_servers, bool unreliable=false) {
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

        if (unreliable) {
            for (int i = 0; i < n_servers; i ++) {
                rafts_[i]->SetReplyUnreliable(true);
            }
        }


        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        ASSERT_EQ(true, checkOneLeader(rafts_).first);
        ASSERT_EQ(true, checkTermsSame(rafts_));
    }
    int n_servers_{0};
    std::vector<RaftPtr> rafts_;
};


TEST_F(UnreliableTest, UnreliableAgreement) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(5, true);

    wait_group::WaitGroup wg(0);
    int n_iter = 50;
    for (int i = 0; i < n_iter; i ++) {
        for (int j = 0; j < 4; j ++) {
            wg.Add(1);
            std::thread one_thread([&](){
                 ASSERT_NE(-1, one(rafts_, std::to_string(100*i+j), 1));
                 wg.Done();
            });
            one_thread.detach();
        }
        ASSERT_NE(-1, one(rafts_, std::to_string(i), 1));
    }
    for (int i = 0; i < n_servers_; i ++) {
        rafts_[i]->SetReplyUnreliable(false);
    }

    wg.Wait();
    ASSERT_NE(-1, one(rafts_, "100", n_servers_));
}

TEST_F(UnreliableTest, Figure8Test) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(5, false);
    ASSERT_NE(-1, one(rafts_, std::to_string(tcp::RandomPort()), 1));

    int n_iter = 1000;
    int nup = n_servers_;
    for (int iter = 0; iter < n_iter; iter ++) {
        int leader_idx = -1;
        for (int i = 0; i < n_servers_; i ++) {
            auto start_result = rafts_[i]->Start(std::to_string(tcp::RandomPort()));
            bool ok = std::get<2>(start_result);
            if (ok) {
                leader_idx = i;
            }
        }

        if (GetRandomInt(0, 1000) < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        if (leader_idx != -1) {
            Raft::Crash(rafts_[leader_idx]);
            nup -= 1;
        }

        if (nup < 3) {
            int s = GetRandomInt(0, n_servers_-1);
            if (rafts_[s]->Killed()) {
                crashRestart(rafts_, s);
                nup += 1;
            }
        }
        spdlog::debug("ITERATION[{}]完成", iter);
    }

    for (int i = 0; i < n_servers_; i ++) {
        if (rafts_[i]->Killed()) {
            crashRestart(rafts_, i);
        }
    }

    ASSERT_NE(-1, one(rafts_, std::to_string(GetRandomInt(100, 1000)), n_servers_));

}