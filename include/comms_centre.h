#pragma once

#include "tcp/tcp_client.h"
#include "rpc/rpc_server.h"

#include <unordered_map>
#include <memory>
#include <string>
#include <mutex>


namespace raft {

using Client = tcp::TcpClient;
using ClientPtr = std::shared_ptr<Client>;

using Server = rpc::RpcServer;
using ServerPtr = std::shared_ptr<Server>;

struct PeerInfo {
    std::string ip;
    uint16_t port;
};



class CommsCentre {
protected:
    std::unordered_map<std::string, PeerInfo> peers_;
    std::unordered_map<std::string, ClientPtr> pipes_with_peer_;
    ServerPtr rpc_server_;
    std::string name_;
    uint16_t port_;

public:
    explicit CommsCentre(std::string name, uint16_t port);
    virtual ~CommsCentre();

    virtual void AddPeer(const std::string &name, const PeerInfo &peer);

    bool OpenService();
    int ConnectTo(const std::string& dst="");

    bool Call(
            const std::string &peer_name,
            const std::string &method_name,
            const std::string &serialized_send,
            std::string &serialized_recv);

    void CloseRPC();

protected:
    bool _connect(const std::string & dst);

};



}