
#include "raft.h"
#include "log_entry.h"
#include "append_entries.h"
#include "request_votes.h"

#include "spdlog/spdlog.h"

#include <utility>
#include <vector>
#include <atomic>
#include <thread>

namespace raft {

/*
 * 3个计时器:
 *      election_timeout_trigger_ ----> 超时之后开始竞选领导者
 *      replicate_cycle_timer_    ----> 定期发送AppendEntries请求[如果是领导者]
 *      apply_cycle_timer_        ----> 定期把committed日志apply到状态机
 *
 * 下面用三元组表示三个计时器开关情况, 例如 [off, on, on]
 *
 * 1. 启动后默认身份是Follower                         [on, off, on]
 *
 * 2. ElectionTimeout, Follower ----> Candidate     [on, off, on] --> [on, off, on]
 * 3. Candidate ----> Leader                        [on, off, on] --> [off, on, on]
 * 4. Candidate ----> Follower                      [on, off, on] --> [on, off, on]
 * 5. Leader ----> Follower                         [off, on, on] --> [on, off, on]
 *
 * 6. Follower ---> Leader                          [不可能出现]
 * 7. Leader ---> Candidate                         [不可能出现]
 *
 *
 * 总结:
 *      --> Candidate 打开election_timeout_trigger_, 关闭replicate_cycle_timer_
 *      --> Follower  打开election_timeout_trigger_, 关闭replicate_cycle_timer_
 *      --> Leader    打开replicate_cycle_timer_, 关闭election_timeout_trigger_
 *
 * */


RaftPtr Raft::Make(
        const std::vector<PeerInfo> &peers_info,
        const std::vector<std::string> &peers_name,
        const std::string &me, uint16_t port) {
    RaftPtr rf = std::make_shared<Raft>(me, port);
    int n_peers = int(peers_info.size());
    assert ( n_peers > 0 );
    for (int i = 0; i < n_peers; i ++) {
        if (peers_name[i] == me) continue;
        rf->AddPeer(peers_name[i], peers_info[i]);
    }

    rf->TurnOn();

    return rf;
}


//RaftPtr Raft::Make(
//        const std::vector<PeerInfo> &peers_info,
//        const std::vector<std::string> &peers_name,
//        const std::string &me, uint16_t port) {
//    RaftPtr rf = std::make_shared<Raft>(me, port);
//    int n_peers = int(peers_info.size());
//    assert ( n_peers > 0 );
//    for (int i = 0; i < n_peers; i ++) {
//        if (peers_name[i] == me) continue;
//        rf->AddPeer(peers_name[i], peers_info[i]);
//        rf->next_index_.insert({peers_name[i], 0});
//        rf->match_index_.insert({peers_name[i], 0});
//    }
//    rf->peers_num_ = n_peers;
//    rf->current_term_ = 0;
//    rf->vote_for_ = "None";
//    rf->logs_.push_back(default_empty_log);  // 占位. 这么做可以保证next_index_, match_index_初始值0有意义
//    rf->commit_index_ = 0;
//    rf->last_applied_ = 0;
//    rf->status_ = RaftStatus::Follower;
//    rf->dead_.store(false);
//    rf->votes_to_me_ = 0;
//
//    rf->election_timeout_ = GetRandDuration(ElectionTimeoutMin, ElectionTimeoutMax);
//    rf->last_recv_time_ = Clock::now();
//    rf->heart_beat_timeout_ = HeartBeatInterval;
//    rf->last_heart_beat_time_ = Clock::now();
//
//    rf->OpenService();  // 打开自己的Rpc服务器
//    rf->ConnectTo();    // 与所有peers建立Tcp连接
//
//
//    auto rf_weak_ptr = std::weak_ptr<Raft>(rf);  // 防止循环引用
//
//    rf->election_timeout_trigger_.SetUpTimeout(rf->election_timeout_);
//    auto election_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_electionHandler(); };
//    rf->election_timeout_trigger_.SetUpCallback(election_callback);
//
//    rf->replicate_cycle_timer_.SetUpTimeout(rf->heart_beat_timeout_);
//    auto replicate_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_replicateHandler(); };
//    rf->replicate_cycle_timer_.SetUpCallback(replicate_callback);
//
//    rf->apply_cycle_timer_.SetUpTimeout(ApplyInterval);
//    auto apply_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_applyHandler(); };
//    rf->apply_cycle_timer_.SetUpCallback(apply_callback);
//
//
//    rf->election_timeout_trigger_.Start();
//    rf->replicate_cycle_timer_.Start();
//    rf->apply_cycle_timer_.Start();
//
//    return rf;
//}


Raft::Raft(std::string name, uint16_t port)
    : CommsCentre(std::move(name), port),
      peers_num_(0),
      current_term_(0),
      vote_for_("None"),
      commit_index_(0),
      last_applied_(0),
      status_(RaftStatus::Follower),
      dead_(false),
      votes_to_me_(0),
      election_timeout_(GetRandDuration(ElectionTimeoutMin, ElectionTimeoutMax)),
      last_recv_time_(Clock::now()),
      heart_beat_timeout_(HeartBeatInterval),
      last_heart_beat_time_(Clock::now()) {
    logs_.push_back(default_empty_log);
}

Raft::~Raft() {
    spdlog::debug("Raft Node [{}] is deconstructing. ");
}


void Raft::TurnOn() {
    OpenService();  // 打开自己的Rpc服务器
    ConnectTo();    // 与所有peers建立Tcp连接

    auto rf_weak_ptr = std::weak_ptr<Raft>(shared_from_this());  // 防止循环引用

    election_timeout_trigger_.SetUpTimeout(election_timeout_);
    auto election_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_electionHandler(); };
    election_timeout_trigger_.SetUpCallback(election_callback);

    replicate_cycle_timer_.SetUpTimeout(heart_beat_timeout_);
    auto replicate_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_replicateHandler(); };
    replicate_cycle_timer_.SetUpCallback(replicate_callback);

    apply_cycle_timer_.SetUpTimeout(ApplyInterval);
    auto apply_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_applyHandler(); };
    apply_cycle_timer_.SetUpCallback(apply_callback);


    election_timeout_trigger_.Start();
    replicate_cycle_timer_.Start();
    apply_cycle_timer_.Start();
}



void Raft::AddPeer(const std::string &name, const PeerInfo &peer) {
    if (name == name_) {
        return;
    }
    CommsCentre::AddPeer(name, peer);
    next_index_.insert({name, 0});
    match_index_.insert({name, 0});
    peers_num_ ++;
}


std::tuple<int, int, bool> Raft::Start(const std::string &msg) {
    int index = -1;

    std::unique_lock<std::mutex> lock(mu_);
    bool is_leader = ( status_ == RaftStatus::Leader );
    int term = current_term_;

    if (is_leader) {
        // TODO
    }
    return { index, term, is_leader };
}

void Raft::Kill() {
    dead_.store(true);
}

bool Raft::Killed() {
    return dead_.load();
}

std::pair<int, bool> Raft::GetState()  {
    std::unique_lock<std::mutex> lock(mu_);
    return {current_term_, ( status_ == RaftStatus::Leader )};
}

/// 没有mutex保护
void Raft::_toCandidate() {
    current_term_ ++;
    status_ = RaftStatus::Candidate;
    vote_for_ = name_;
    votes_to_me_ = 1;

    last_recv_time_ = Clock::now();
    // 启动一个新的election_timeout_trigger_
    election_timeout_trigger_.Stop();
    election_timeout_ = GetRandDuration(ElectionTimeoutMin, ElectionTimeoutMax);
    election_timeout_trigger_.SetUpTimeout(election_timeout_);
    election_timeout_trigger_.Start();
    // 关闭replicate_cycle_timer_
    replicate_cycle_timer_.Stop();
}

/// 没有mutex保护
void Raft::_toFollower() {
    status_ = RaftStatus::Follower;
    votes_to_me_ = 0;
    vote_for_ = "None";

    last_recv_time_ = Clock::now();

    // 启动一个新的election_timeout_trigger_
    election_timeout_trigger_.Stop();
    election_timeout_ = GetRandDuration(ElectionTimeoutMin, ElectionTimeoutMax);
    election_timeout_trigger_.SetUpTimeout(election_timeout_);
    election_timeout_trigger_.Start();
    // 关闭replicate_cycle_timer_
    replicate_cycle_timer_.Stop();
}

/// 没有mutex保护
void Raft::_toLeader() {
    status_ = RaftStatus::Leader;
    votes_to_me_ = 0;
    vote_for_ = "None";
    last_heart_beat_time_ = Clock::now();
    for (auto & it : match_index_) {
        it.second = 0;
        next_index_[it.first] = int(logs_.size());
    }

    // 关闭election_timeout_trigger_
    election_timeout_trigger_.Stop();
    // 关闭replicate_cycle_timer_
    replicate_cycle_timer_.Stop();
    replicate_cycle_timer_.Start();
}

// ElectionTimeout后怎么办？
void Raft::_electionHandler() {
    std::unique_lock<std::mutex> lock(mu_);
    // 有没有必要检查一下身份？
    if (status_ == RaftStatus::Leader) {
        spdlog::debug("Election time out, however I am the Leader now ?!");
        return;
    }
    _toCandidate();

    // broadcast RequestVotes RPC
    const LogEntry & end_log = logs_.back();
    RequestVoteArgs args { current_term_, name_, end_log.index, end_log.term };
    auto message = args.Serialization();
    for (auto & peer_pipe: pipes_with_peer_) {
        assert (peer_pipe.first != name_);
        auto sender = peer_pipe.second;
        std::thread th_send([sender, &message](){
            sender->SendMsg(message);
        });
    }

}

// time to replicate
void Raft::_replicateHandler() {
    std::unique_lock<std::mutex> lock(mu_);
    // 有没有必要检查一下身份？
    if (status_ != RaftStatus::Leader) {
        spdlog::debug("Time to replicate log entries, however I am not the Leader now ?!");
        return;
    }
    // 这个变量貌似没有用了, 因为replicate_cycle_timer_内部可以处理循环计时
    last_heart_beat_time_ = Clock::now();
    // TODO: broadcast AppendEntries RPC
    const LogEntry & end_log = logs_.back();
    AppendEntriesArgs args { &logs_, int(logs_.size()) }; // 定义一个心跳包
    args.term = current_term_;
    args.leader_name = name_;
    args.leader_committed_index = commit_index_;
    args.prev_log_index = end_log.index;
    args.prev_log_term = end_log.term;

    int n_log_in_leader = int(logs_.size());

    int n_thread = int(pipes_with_peer_.size());
    std::vector<std::thread> send_threads(n_thread);

    int i = -1;
    for (auto & peer_pipe: pipes_with_peer_) {
        assert ( peer_pipe.first != name_ );
        i ++;
        int next_index = next_index_[peer_pipe.first];
        int match_index = match_index_[peer_pipe.first];
        if (next_index == n_log_in_leader && match_index == next_index-1) {
            auto sender = peer_pipe.second;
            send_threads[i] = std::thread([sender, args](){ // 心跳包直接发送args就可以了
                sender->SendMsg(args.Serialization());
            });
        } else {
            const LogEntry & prev_log = logs_[next_index - 1];
            int prev_log_index = prev_log.index;
            int prev_log_term = prev_log.term;

            auto sender = peer_pipe.second;
            send_threads[i] = std::thread([sender, prev_log_index, prev_log_term, next_index](AppendEntriesArgs &args){
                // AppendEntriesArgs &做一次拷贝, AppendEntriesArgs拷贝两次
                // args用拷贝, 防止循环下一轮修改args, 影响到本轮的发送
                args.prev_log_index = prev_log_index;
                args.prev_log_term = prev_log_term;
                args.send_start_index = next_index;
                sender->SendMsg(args.Serialization());
            }, args);
        }
    }

    for (i = 0; i < n_thread; i ++) { // 用来发送数据的线程没有被保护, 所有要join, _replicateHandler等到发送完毕再退出
        if (send_threads[i].joinable()) {
            send_threads[i].join();
        }
    }
}

// time to apply
void Raft::_applyHandler() {
    std::unique_lock<std::mutex> lock(mu_);
    while (last_applied_ < commit_index_) {
        last_applied_ ++;
        // TODO: apply一条日志
    }
}


}