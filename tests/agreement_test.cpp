
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "raft_test_common.h"

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
    int n_servers_;
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