
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "raft_test_common.h"
#include "wait_group/wait_group.h"

using namespace raft;


class AgreementTest: public testing::Test {
public:
    AgreementTest() = default;
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


TEST_F(AgreementTest, BasicAgreementTest) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(5);
    std::pair<bool, RaftPtr> check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, true);
    ASSERT_NE(check_result.second, nullptr);
    RaftPtr leader = check_result.second;

    int iters = 3;
    std::string command;
    for (int index = 1; index <= iters; index ++) {
        auto commit_result = nCommitted(rafts_, index);
        ASSERT_EQ(0, commit_result.first);  // 1号位置还没有日志, 所以应该返回0
        command = std::to_string(index*100);
        int committed_index = one(rafts_, command, n_servers_);
        ASSERT_EQ(committed_index, index);
        spdlog::info("达成共识: index=[{}], command=[{}]", index, command);
    }
}

TEST_F(AgreementTest, AgreementOnOneNodeFailTest) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(3);
    int committed_index = one(rafts_, "101", n_servers_);
    ASSERT_EQ(committed_index, 1);
    std::pair<int, RaftPtr> check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, 1);
    RaftPtr leader = check_result.second;
    ASSERT_NE(leader, nullptr);

    RaftPtr follower = killOneFollower(rafts_);
    spdlog::debug("结点[{}]下线\n\n\n\n", follower->GetName());
    committed_index = one(rafts_, "102", n_servers_-1);
    ASSERT_EQ(committed_index, 2);
    committed_index = one(rafts_, "103", n_servers_-1);
    ASSERT_EQ(committed_index, 3);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    committed_index = one(rafts_, "104", n_servers_-1);
    ASSERT_EQ(committed_index, 4);
    committed_index = one(rafts_, "105", n_servers_-1);
    ASSERT_EQ(committed_index, 5);

    follower->Recover();
    spdlog::debug("结点[{}]上线\n\n\n\n", follower->GetName());

    committed_index = one(rafts_, "106", n_servers_);
    ASSERT_EQ(committed_index, 6);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    committed_index = one(rafts_, "107", n_servers_);
    ASSERT_EQ(committed_index, 7);
};


TEST_F(AgreementTest, NoAgreementTest) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(5);
    int committed_index = one(rafts_, "10", n_servers_);
    ASSERT_EQ(committed_index, 1);

    auto check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, 1);
    RaftPtr leader = check_result.second;
    ASSERT_NE(leader, nullptr);

    RaftPtr follower_1 = killOneFollower(rafts_);
    RaftPtr follower_2 = killOneFollower(rafts_);
    RaftPtr follower_3 = killOneFollower(rafts_);
    ASSERT_NE(follower_1->GetName(), follower_2->GetName());
    ASSERT_NE(follower_1->GetName(), follower_3->GetName());
    ASSERT_NE(follower_2->GetName(), follower_3->GetName());
    ASSERT_EQ(follower_1->Killed(), true);
    ASSERT_EQ(follower_2->Killed(), true);
    ASSERT_EQ(follower_3->Killed(), true);
    spdlog::debug("结点[{},{},{}]下线\n\n\n\n", follower_1->GetName(), follower_2->GetName(), follower_3->GetName());

    auto start_result = leader->Start("20");
    int index = std::get<0>(start_result);
    bool ok = std::get<2>(start_result);

    ASSERT_EQ(ok, true);  // 确认leader还是领导者

    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    std::pair<int, std::string> commit_result = nCommitted(rafts_, index);
    ASSERT_GE(0, commit_result.first);  // 应该无法达成一致

    follower_1->Recover();
    follower_2->Recover();
    follower_3->Recover();
    spdlog::debug("结点[{},{},{}]上线\n\n\n\n", follower_1->GetName(), follower_2->GetName(), follower_3->GetName());


    check_result = checkOneLeader(rafts_);
    ASSERT_EQ(check_result.first, 1);
    leader = check_result.second;
    ASSERT_NE(leader, nullptr);

    start_result = leader->Start("30");
    index = std::get<0>(start_result);
    ok = std::get<2>(start_result);

    ASSERT_NE(ok, false);
    ASSERT_GE(index, 2);
    ASSERT_LE(index, 3);

    one(rafts_, "1000", n_servers_);

}


TEST_F(AgreementTest, ConcurrentStartTest) {
    spdlog::set_level(spdlog::level::debug);
    StartUp(3);

    bool success = false;

    int attempt = 0;

    while (true) {
        bool breakout = true;
        for ( ; attempt < 5; attempt ++) {
            spdlog::debug("第{}次尝试", attempt);
            if (attempt > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(3000));
            }
            auto check_result = checkOneLeader(rafts_);
            ASSERT_EQ(check_result.first, true);
            RaftPtr leader = check_result.second;
            auto start_result = leader->Start("1");
            bool ok = std::get<2>(start_result);
            int term = std::get<1>(start_result);
            if (!ok) {
                continue;
            }

            int iter = 5;
            std::vector<std::thread> workers(iter);

            std::mutex set_mutex;
            std::set<int> indexes;
            wait_group::WaitGroup wg(0);

            for (int ii = 0; ii < iter; ii ++) {
                wg.Add(1);
                workers[ii] = std::thread([ii, &wg, leader, term, &indexes, &set_mutex](){
                    std::string cmd = std::to_string(ii+100);
                    auto start_result = leader->Start(cmd);
                    int local_index = std::get<0>(start_result);
                    int local_term = std::get<1>(start_result);
                    bool local_ok = std::get<2>(start_result);
                    if (local_term != term || !local_ok) {
                        return;
                    }
                    {
                        std::unique_lock<std::mutex> lock(set_mutex);
                        indexes.insert(local_index);
                    }
                    wg.Done();
                });
            }
            spdlog::debug("等待线程完成Start工作");
            wg.Wait();
            spdlog::debug("Start工作已完成");

            for (int j = 0; j < n_servers_; j ++) {
                auto raft_state = rafts_[j]->GetState();
                int raft_term = raft_state.first;
                if (raft_term != term) {
                    breakout = false;
                    break;
                }
            }

            if (!breakout) {
                continue;
            }

            bool failed = false;
            std::vector<std::string> cmds;
            for (int idx : indexes) {
                std::string cmd = configWait(rafts_, idx, n_servers_, term);
                if (cmd == "-1") {
                    failed = true;
                    break;
                }
                cmds.push_back(cmd);
            }


            for(auto & worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }

            for (int ii = 0; ii < iter; ii ++) {
                int x = 100 + ii;
                bool ok = false;
                for (auto & cmd : cmds) {
                    if (std::stoi(cmd) == x) {
                        ok = true;
                    }
                }
                ASSERT_EQ(ok, true);
            }

            success = true;
            break;
        }

        if (attempt == 5) {
            spdlog::error("尝试次数超过极限, 说明Raft集群term变化太频繁");
            break;
        }

        if (breakout) {
            break;
        }
    }

    ASSERT_NE(attempt, 5);
}