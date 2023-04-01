
#include "node.h"

namespace raft {


RaftNode::RaftNode(std::string name, uint16_t port)
    : name_(std::move(name)), port_(port) {
    rpc_server_ = std::make_shared<Server>("[RPC Server]", port);
    rpc_server_->Register("Echo", [](const std::string &recv, std::string &reply){
        reply = recv;
    });
    rpc_server_->HandleReceiveData([&](const std::string &recv, std::string &reply){
        rpc_server_->Call<const std::string&, std::string &>("Echo", recv, std::ref(reply));
    });
}


RaftNode::~RaftNode() {
    spdlog::info("{} is being deconstructing.", name_);
    CloseRPC();
    peers_.clear();
    pipes_with_peer_.clear();
}

void RaftNode::CloseRPC() {
    rpc_server_.reset();
}

//void RaftNode::DropAllConnections() {
//
//}


void RaftNode::AddPeer(const std::string &name, const PeerInfo &peer) {
    peers_[name] = peer;
    pipes_with_peer_[name] = std::make_shared<Client>();
}

bool RaftNode::Start() {
    return rpc_server_->Start();
}


int RaftNode::ConnectTo(const std::string& dst) {
    int result = 0;
    if (dst.empty()) { // connect to all peers
        for (const auto& it: peers_) {
            result += _connect(it.first);
        }
    } else {
        result += _connect(dst);
    }

    return result;
}

bool RaftNode::_connect(const std::string &dst) {
    auto it = peers_.find(dst);
    if (it == peers_.end()) return false;
    auto pipe = pipes_with_peer_.find(dst);
    assert( pipe != pipes_with_peer_.end() );
    return pipe->second->ConnectTo(it->second.ip, it->second.port, 20);
}


bool RaftNode::Call(const std::string &peer_name, const std::string &method_name, const std::string &serialized_send,
                    std::string &serialized_recv) {
    assert ( method_name == "Echo" ); // TODO: delete this line
    auto it = peers_.find(peer_name);
    if (it == peers_.end()) {
        return false;
    }
    // TODO: 假设已经连接上了, 后面还要加检查
    auto pipe = pipes_with_peer_.find(peer_name);
    assert ( pipe != pipes_with_peer_.end() );
    if (!pipe->second->SendMsg(serialized_send)) {
        return false;
    }
    if (!pipe->second->RecvMsg(&serialized_recv)) {
        return false;
    }

    return true;
}




}
