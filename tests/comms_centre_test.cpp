
#include "gtest/gtest.h"
#include "rpc/rpc_server.h"
#include "comms_centre.h"

#include <thread>
#include <future>
#include <memory>
#include <vector>

using namespace raft;

using CommsCentrePtr = std::shared_ptr<CommsCentre>;
using NodeFuture = std::future<CommsCentrePtr>;

TEST(CommsCentreTest, CommunicateTest) {

    spdlog::set_level(spdlog::level::debug);

    int N = 8;
    int base = 1121;
    std::vector<uint16_t> ports(N);
    std::vector<std::string> names(N);
    std::vector<std::string> send_msgs(N);

    for (int i = 0; i < N; i ++) {
        ports[i] = (i+1)*base;
        names[i] = std::to_string(i);
        send_msgs[i] = std::string("hello, I am ") + names[i];
    }

    std::vector<NodeFuture> node_futures(N);

    auto create_node = [&](int id) -> CommsCentrePtr {
        CommsCentrePtr node_ptr = std::make_shared<CommsCentre>(names[id], ports[id]);
        for (int i = 0; i < N; i ++) {
            if (i != id) {
                node_ptr->AddPeer(names[i], {"127.0.0.1", ports[i]});
            }
        }
        bool success = node_ptr->OpenService();
        if (!success) {
            spdlog::error("{} OpenService failed. ", names[id]);
            return nullptr;
        }

        int n_connect = node_ptr->ConnectTo();
        spdlog::info("{} connect to {} peers successfully", names[id], n_connect);
        return node_ptr;
    };

    for (int i = 0; i < N; i ++) {
        node_futures[i] = std::async(create_node, i);
    }

}





