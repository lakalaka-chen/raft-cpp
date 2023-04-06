
#include "raft.h"

#include "spdlog/spdlog.h"

#include <utility>
#include <vector>
#include <atomic>
#include <thread>
#include <sstream>
#include <random>

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

using trigger_timer::Clock;

int GetRandomInt(int min, int max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(min, max);
    return dis(gen);
}

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

//    rf->TurnOn();

    return rf;
}


Raft::Raft(std::string name, uint16_t port)
    : CommsCentre(std::move(name), port),
      peers_num_(1),
      current_term_(0),
      vote_for_("None"),
      commit_index_(0),
      last_applied_(0),
      status_(RaftStatus::Follower),
      dead_(false),
      votes_to_me_(0),
      election_timeout_(GetRandomInt(ElectionTimeoutMin, ElectionTimeoutMax)),
      heart_beat_timeout_(HeartBeatInterval) {
    logs_.push_back(default_empty_log);
}

Raft::~Raft() {
    spdlog::debug("结点[{}]正在退出... ", name_);
}


void Raft::SetUp() {
    _installRpcService();           // 先注册一下RPC服务
    _installReceiveHandler();       // 确定收到数据包的解析、流程
    OpenService();                  // 打开自己的Rpc服务器
    _installTimers();               // 设置计时器的行为和倒计时
}

void Raft::_installTimers() {
    auto rf_weak_ptr = std::weak_ptr<Raft>(shared_from_this());  // 防止循环引用

    election_timeout_trigger_.SetUpTimeout(election_timeout_);
    election_callback_ = [rf_weak_ptr] { rf_weak_ptr.lock()->_electionHandler(); };
    election_timeout_trigger_.SetUpCallback(election_callback_);

    replicate_cycle_timer_.SetUpTimeout(heart_beat_timeout_);
    auto replicate_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_replicateHandler(); };
    replicate_cycle_timer_.SetUpCallback(replicate_callback);

    apply_cycle_timer_.SetUpTimeout(ApplyInterval);
    auto apply_callback = [rf_weak_ptr] { rf_weak_ptr.lock()->_applyHandler(); };
    apply_cycle_timer_.SetUpCallback(apply_callback);
    spdlog::debug("结点[{}]初始化计时器完毕", name_);
}


void Raft::StartTimers(bool enable_candidate) {
    if (enable_candidate) {
        election_timeout_trigger_.Start();
    }
//    apply_cycle_timer_.Start();
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
}

/// 这个函数的内容本来应该放在_toCandidate()内部
/// 结点变成Candidate之后, RequestVotes还没发完
/// 又超时了
/// 所以改变策略, 等发完了再执行这个函数的操作
void Raft::_restartElectionTimeout() {
    // 启动一个新的election_timeout_trigger_
    election_timeout_trigger_.Stop();
    election_timeout_ = GetRandomInt(ElectionTimeoutMin, ElectionTimeoutMax);
    election_timeout_trigger_.SetUpTimeout(election_timeout_);
//    election_timeout_trigger_.SetUpCallback(election_callback_);
    election_timeout_trigger_.Start();
    // 关闭replicate_cycle_timer_
    replicate_cycle_timer_.Stop();
}

/// 没有mutex保护
void Raft::_toFollower() {
    status_ = RaftStatus::Follower;
    votes_to_me_ = 0;
    vote_for_ = "None";

    _restartElectionTimeout();
}

/// 没有mutex保护
void Raft::_toLeader() {
    status_ = RaftStatus::Leader;
    votes_to_me_ = 0;
    vote_for_ = "None";
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
        spdlog::debug("结点[{}]超时, 但是临时发现已经是领导者", name_);
        return;
    }
    spdlog::debug("结点[{}]超时, 成为竞选者", name_);
    _toCandidate();

    // broadcast RequestVotes RPC
    const LogEntry & end_log = logs_.back();
    RequestVoteArgs args { current_term_, name_, end_log.index, end_log.term };

    lock.unlock();
    for (auto & peer_pipe: pipes_with_peer_) {
        assert (peer_pipe.first != name_);
        auto sender = peer_pipe.second;
        std::string dst = peer_pipe.first;
        std::thread th_send([&, args, sender, dst](){
            std::string message = args.Serialization();
            std::string reply_msg;
            spdlog::debug("结点[{}]向结点[{}]发送RequestVotesRPC请求, 数据包序列化后的内容「 {}」", name_, dst, message);
            sender->SendMsg(message);
            sender->RecvMsg(&reply_msg);
            spdlog::debug("结点[{}]收到结点[{}]的投票回复, 数据包序列化后的内容「 {}」", name_, dst, reply_msg);
            RequestVoteReply reply;
            bool ok = RequestVoteReply::UnSerialization(reply_msg, reply);
            if (!ok) {
                spdlog::error("结点[{}]收到结点[{}]的投票回复内容解析出错", name_, dst);
            } else {
                std::unique_lock<std::mutex> lock(mu_);
                if (reply.term > current_term_) {
                    spdlog::debug("结点[{}]的任期号{}, 结点[{}]的任期号{}, 竞选失败", name_, current_term_, dst, reply.term);
                    _toFollower();
                } else if (reply.vote_granted) {
                    votes_to_me_ ++;
                    if (votes_to_me_ * 2 > peers_num_) {
                        spdlog::debug("结点[{}]收到多数派投票, 成为领导者, 任期号{}", name_, current_term_);
                        _toLeader();
                    }
                } else {
                    spdlog::error("竞选失败, err_msg= {}", reply.err_msg);
                }
            }
        });
        th_send.detach();
    }

//    lock.lock();
//    _restartElectionTimeout();
}

// time to replicate
void Raft::_replicateHandler() {
    std::unique_lock<std::mutex> lock(mu_);
    // 有没有必要检查一下身份？
    if (status_ != RaftStatus::Leader) {
        spdlog::debug("结点[{}]准备发送AppendEntries, 但是临时发现不是领导者", name_);
        return;
    }

    // broadcast AppendEntries RPC
    const LogEntry & end_log = logs_.back();
    AppendEntriesArgs args { &logs_, int(logs_.size()) }; // 定义一个心跳包
    args.term = current_term_;
    args.leader_name = name_;
    args.leader_committed_index = commit_index_;
    args.prev_log_index = end_log.index;
    args.prev_log_term = end_log.term;

    int n_log_in_leader = int(logs_.size());

    int n_thread = int(pipes_with_peer_.size());
    lock.unlock();

    std::vector<std::thread> send_threads(n_thread);

    int i = -1;
    for (auto & peer_pipe: pipes_with_peer_) {
        lock.lock();
        assert ( peer_pipe.first != name_ );
        i ++;
        int next_index = next_index_[peer_pipe.first];
        int match_index = match_index_[peer_pipe.first];
        if (next_index == n_log_in_leader && match_index == next_index-1) {
            auto sender = peer_pipe.second;
            std::string dst = peer_pipe.first;
            lock.unlock();
            send_threads[i] = std::thread([&, sender, args, dst](){ // 心跳包直接发送args就可以了
                std::string message = args.Serialization();
                sender->SendMsg(message);
                spdlog::debug("结点[{}]向结点[{}]发送心跳包, 序列化后的数据包内容: 「 {}」", name_, dst, message);
                std::string reply_msg;
                sender->RecvMsg(&reply_msg);
                spdlog::debug("结点[{}]收到结点[{}]的心跳包回复, 序列化后的数据包内容: 「 {}」", name_, dst, reply_msg);
                std::unique_lock<std::mutex> lock(mu_);
                AppendEntriesReply reply;
                bool ok = AppendEntriesReply::UnSerialization(reply_msg, reply);
                if (!ok) {
                    spdlog::error("结点[{}]收到结点[{}]的心跳包回复, 但是解析出错", name_, dst);
                    return;
                }
                if (status_ != RaftStatus::Leader) {
                    spdlog::debug("结点[{}]收到结点[{}]的心跳包回复, 但是此时已经不是领导者", name_, dst);
                    return;
                }
                if (reply.term > current_term_) {
                    spdlog::debug("结点[{}]收到结点[{}]的心跳包回复, 发现自己过期, {}<{}", name_, dst, current_term_, reply.term);
                    current_term_ = reply.term;
                    _toFollower();
                    return;
                }
            });
        } else {
            const LogEntry & prev_log = logs_[next_index - 1];
            int prev_log_index = prev_log.index;
            int prev_log_term = prev_log.term;

            auto sender = peer_pipe.second;
            args.prev_log_index = prev_log_index;
            args.prev_log_term = prev_log_term;
            args.send_start_index = next_index;
            lock.unlock();
            send_threads[i] = std::thread([&, sender, args](){
                // AppendEntriesArgs &做一次拷贝, AppendEntriesArgs拷贝两次
                // args用拷贝, 防止循环下一轮修改args, 影响到本轮的发送
                sender->SendMsg(args.Serialization());
                std::string reply_msg;
                sender->RecvMsg(&reply_msg);
                std::unique_lock<std::mutex> lock(mu_);
                AppendEntriesReply reply;
                bool ok = AppendEntriesReply::UnSerialization(reply_msg, reply);
                if (!ok) {
                    spdlog::error("AppendEntriesRPC收到错误的回复类型");
                    return;
                }
                if (status_ != RaftStatus::Leader) {
                    return;
                }

                if (reply.term > current_term_) {
                    spdlog::info("收到[{}]的AppendEntries回复, 发现自己过期[收到term= {}, 自己的term= {}]", reply.server_name, reply.term, current_term_);
                    current_term_ = reply.term;
                    _toFollower();
                    return;
                }

                if (reply.success) {
                    match_index_[reply.server_name] = reply.finished_index;
                    next_index_[reply.server_name] = reply.finished_index + 1;
                    if (_isEnableCommit(reply.finished_index)) {
                        commit_index_ = reply.finished_index;
                    }
                } else {
                    next_index_[reply.server_name] = reply.conflict_index;
                }

            });
        }
    }

    for (i = 0; i < n_thread; i ++) {
//        if (send_threads[i].joinable()) {
//            send_threads[i].join();
//        }
        send_threads[i].detach();
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



void Raft::_installRpcService() {
    // TODO: 暂时不用RPC服务的做法
}

void Raft::_installReceiveHandler() {

    auto raft_ptr = std::weak_ptr<Raft>(shared_from_this());
    rpc_server_->HandleReceiveData([raft_ptr](const std::string &recv, std::string &reply){
        spdlog::debug("结点[{}]收到消息: 「 {}」", raft_ptr.lock()->name_, recv);
        std::istringstream is(recv);
        std::string package_name;
        is >> package_name;
        std::unique_lock<std::mutex> lock(raft_ptr.lock()->mu_);
        if (package_name == "RequestVotes") {
            RequestVoteReply vote_reply;
            bool analysis_success = raft_ptr.lock()->_requestVotes(is, vote_reply);
            if (!analysis_success) {
                spdlog::error("结点[{}]解析RequestVotes出错", raft_ptr.lock()->name_);
                reply = "[package error]";
            } else {
                reply = vote_reply.Serialization();
            }
        } else if (package_name == "AppendEntries") {
            spdlog::debug("结点[{}]收到AppendEntries请求", raft_ptr.lock()->name_);
            AppendEntriesReply append_reply;
            bool analysis_success = raft_ptr.lock()->_appendEntries(is, append_reply);
            if (!analysis_success) {
                spdlog::error("结点[{}]解析AppendEntries出错", raft_ptr.lock()->name_);
                reply = "[package error]";
            } else {
                reply = append_reply.Serialization();
            }
        } else {
            spdlog::error("结点[{}]收到未知类型的数据包", raft_ptr.lock()->name_);
            reply = "Unknown type RPC. ";
        }
    });
}

/*
 * os << term << " ";
 * os << leader_name << " ";
 * os << leader_committed_index << " ";
 * os << prev_log_index << " ";
 * os << prev_log_term << " ";
 * int n_logs = int(logs_ptr->size());
 * os << n_logs-send_start_index << " ";
 * for (int i = send_start_index; i < n_logs; i ++) {
 *     os << (*logs_ptr)[i].Serialization();
 * }
 * */
bool Raft::_appendEntries(std::istringstream &is, AppendEntriesReply &reply) {
    int term;
    std::string leader_name;
    int leader_committed_index;
    int prev_log_index;
    int prev_log_term;
    int n_logs;
    try {
        is >> term >> leader_name >> leader_committed_index;
        is >> prev_log_index >> prev_log_term;
        is >> n_logs;

        if (term < current_term_) {
            reply.term = current_term_;
            reply.success = false;
            return true;  // 解析成功
        }

        reply.server_name = name_;
        current_term_ = term;
        _toFollower();
        reply.term = term;

        if (prev_log_index >= logs_.size()) {
            reply.conflict_index = int(logs_.size());
            reply.success = false;
        } else if (prev_log_term != logs_[prev_log_index].term) {
            int conflict_term = logs_[prev_log_index].term;
            int j = 0;
            for (; j < prev_log_index; j ++) {
                if (logs_[j+1].term == conflict_term) {
                    j = j+1;
                    break;
                }
            }
            reply.conflict_index = j;
            reply.success = false;
        } else {
            logs_.erase(logs_.begin()+prev_log_index+1, logs_.end());
            int n_curr = int(logs_.size());
            logs_.resize( n_curr + n_logs );
            LogEntry log;
            for (int i = 0; i < n_logs; i ++) {
                /*
                 * os << index << " ";
                 * os << term << " ";
                 * os << command << " ";
                 * */
                is >> log.index >> log.term >> log.command;
                logs_[n_curr + i] = log;
            }
            reply.finished_index = logs_.back().index;
            reply.success = true;
            election_timeout_trigger_.Reset();
        }

    } catch (const std::exception &e) {
        spdlog::error("{}", e.what());
        return false;
    }

    return true;
}

/*
    os << term << " ";
    os << candidate_name << " ";
    os << last_log_index << " ";
    os << last_log_term << " ";
*/
bool Raft::_requestVotes(std::istringstream &is, RequestVoteReply &reply) {
    int term;
    std::string candidate_name;
    int last_log_index;
    int last_log_term;
    try {
        is >> term >> candidate_name >> last_log_index >> last_log_term;
        if (current_term_ == term && vote_for_ == candidate_name) {
            reply.vote_granted = true;
            reply.term = current_term_;
            election_timeout_trigger_.Reset();
            return true;
        }

        if (current_term_ == term || ( current_term_ == term && vote_for_ == "None" ) ) {
            reply.term = current_term_;
            reply.vote_granted = false;
            reply.err_msg = "竞选者term落后";
            return true;
        }

        if (current_term_ < term) {
            current_term_ = term;
            vote_for_ = "None";
            if (status_ != RaftStatus::Follower) {
                _toFollower();
            }
        }

        reply.term = term;
        const LogEntry & end_log = logs_.back();

        if (end_log.term > last_log_term || ( end_log.term == last_log_term && end_log.index > last_log_index ) ) {
            reply.vote_granted = false;
            reply.err_msg = "竞选者日志落后";
            return true;
        }

        reply.vote_granted = true;
        election_timeout_trigger_.Reset();
        vote_for_ = candidate_name;
        reply.err_msg = "";
        _toFollower();

    } catch (const std::exception &e) {
        spdlog::error("{}", e.what());
        return false;
    }

    return true;
}

bool Raft::_isEnableCommit(int index) {
    const LogEntry & end_log = logs_.back();
    int n_logs = int(logs_.size());
    if (index < n_logs && commit_index_ < index && logs_[index].term == current_term_) {
        int count = 0;
        for (auto & it : match_index_) {
            if (it.second >= index) {
                count ++;
            }
        }
        return count * 2 > peers_num_;
    }
    return false;
}



}