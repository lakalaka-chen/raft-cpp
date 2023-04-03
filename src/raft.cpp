
#include "raft.h"

#include <vector>
#include <atomic>

namespace raft {


RaftPtr Raft::Make(
        const std::vector<PeerInfo> &peers_info,
        const std::vector<std::string> &peers_name,
        const std::string &me, uint16_t port) {
    RaftPtr rf = std::make_shared<Raft>(me, port);
    int n_peers = int(peers_info.size());
    assert ( n_peers > 0 );
    for (int i = 0; i < n_peers; i ++) {
        rf->AddPeer(peers_name[i], peers_info[i]);
        rf->next_index_.insert({peers_name[i], 0});
        rf->match_index_.insert({peers_name[i], 0});
    }
    rf->peers_num_ = n_peers;
    rf->current_term_ = 0;
    rf->vote_for_ = "None";
    rf->logs_.push_back(default_empty_log);  // 占位. 这么做可以保证next_index_, match_index_初始值0有意义
    rf->commit_index_ = 0;
    rf->last_applied_ = 0;
    rf->status_ = RaftStatus::Follower;
    rf->dead_.store(false);
    rf->votes_to_me_ = 0;

    rf->election_timeout_ = GetRandDuration(ElectionTimeoutMin, ElectionTimeoutMax);
    rf->last_recv_time_ = Clock::now();
    rf->heart_beat_timeout_ = HeartBeatInterval;
    rf->last_heart_beat_time_ = Clock::now();

    rf->OpenService();  // 打开自己的Rpc服务器
    rf->ConnectTo();    // 与所有peers建立Tcp连接


    /*
     * ElectionTimeOut处理
     * 什么时候Start:
     *      1. 变成follower之后要Start
     * 什么时候Reset:
     *      2. 收到有效消息并且自己是follower
     * 什么时候Stop:
     *      3. 成为leader之后要关闭
     *      4. 在candidate状态也要关闭
     * */


    auto rf_weak_ptr = std::weak_ptr<Raft>(rf);  // 防止循环引用

    rf->election_timeout_trigger_.SetUpTimeout(rf->election_timeout_);
    auto election_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_electionHandler(); };
    rf->election_timeout_trigger_.SetUpCallback(election_callback);

    rf->replicate_cycle_timer_.SetUpTimeout(rf->heart_beat_timeout_);
    auto replicate_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_replicateHandler(); };
    rf->replicate_cycle_timer_.SetUpCallback(replicate_callback);

    rf->apply_cycle_timer_.SetUpTimeout(ApplyInterval);
    auto apply_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_applyHandler(); };
    rf->apply_cycle_timer_.SetUpCallback(apply_callback);


    rf->election_timeout_trigger_.Start();
    rf->replicate_trigger_.Start();
    rf->apply_trigger_.Start();

    return rf;
}


//void Raft::_electionMonitor() {
//    std::unique_lock<std::mutex> lock(mu_);
//    if (status_ != RaftStatus::Follower) {
//        return;
//    }
//    _toCandidate();
//
//    // TODO: persist()
//    last_recv_time_ = Clock::now();
//    _broadcastElectionVotes();
//}
//
//void Raft::_replicateMonitor() {
//
//}
//
//void Raft::_applyMonitor() {
//
//}

void Raft::_electionHandler() {

}

void Raft::_replicateHandler() {

}

void Raft::_applyHandler() {

}


}