#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "raft.h"

using namespace raft;


TEST(InitialElection, TEST1) {

    spdlog::set_level(spdlog::level::debug);

    int n_servers = 3;

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
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    for (int i = 0; i < n_servers; i ++) {
        raft_pointers[i]->StartTimers();
    }


    int a;
    while (std::cin >> a) {

    }
}

