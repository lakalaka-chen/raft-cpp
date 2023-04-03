#include "gtest/gtest.h"
#include <string>
#include <vector>

#include "raft.h"
#include "tcp/tcp_server.h"

using namespace raft;


TEST(RaftEndTest, CreateTest) {

    std::vector<PeerInfo> peers_info {
        {"127.0.0.1", tcp::RandomPort()},
        {"127.0.0.1", tcp::RandomPort()},
        {"127.0.0.1", tcp::RandomPort()},
        {"127.0.0.1", tcp::RandomPort()},
    };
    std::vector<std::string> peers_name {
        "Raft Node 2",
        "Raft Node 3",
        "Raft Node 4",
        "Raft Node 5",
    };

    std::string name = "Raft Node 1";
    uint16_t port = tcp::RandomPort();
    RaftPtr end_ptr = Raft::Make(peers_info, peers_name, name, port);



}