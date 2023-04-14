
#include "raft.h"
#include "serializer.h"

#include "spdlog/spdlog.h"

#include <utility>
#include <vector>
#include <atomic>
#include <thread>
#include <sstream>


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
        const std::string &me, uint16_t port, PersisterPtr persister) {
    RaftPtr rf = std::make_shared<Raft>(me, port, persister);
    rf->ReadPersist();  // 读取持久化数据
    int n_peers = int(peers_info.size());
    assert ( n_peers > 0 );
    for (int i = 0; i < n_peers; i ++) {
        if (peers_name[i] == me) continue;
        rf->AddPeer(peers_name[i], peers_info[i]);
    }

    return rf;
}


RaftMetaInfos Raft::Crash(RaftPtr raft_node) {
    RaftMetaInfos meta_infos;
    {
        std::unique_lock<std::mutex> lock(raft_node->mu_);
        meta_infos.me = raft_node->name_;
        meta_infos.persister = raft_node->persister_ptr_->Copy();
        for (const auto &it: raft_node->peers_) {
            meta_infos.peers_info.push_back({it.second.ip, it.second.port});
            meta_infos.peers_name.push_back(it.first);
        }
        meta_infos.listen_port = raft_node->port_;
    }
    raft_node->Kill();

    return meta_infos;
}


Raft::Raft(std::string name, uint16_t port, PersisterPtr persister)
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
      heart_beat_timeout_(HeartBeatInterval),
      persister_ptr_(std::move(persister)),
      rpc_count_(0) {
    logs_.push_back(default_empty_log);
    is_running_ = true;
}


void Raft::Persist() {

    if (persister_ptr_ == nullptr) {
        persister_ptr_ = Persister::MakePersister();
    }

    int node_size = int(sizeof(current_term_) + vote_for_.size() + sizeof(commit_index_) + sizeof(last_applied_));
    int n_logs = int(logs_.size());
    node_size += sizeof(n_logs);
    Serializer serializer(node_size);
    serializer.Encode(current_term_);
    serializer.Encode(vote_for_);
    serializer.Encode(commit_index_);
    serializer.Encode(last_applied_);
    serializer.Encode(n_logs);
    for (int i = 0; i < n_logs; i ++) {
        serializer.EncodeObj(logs_[i]);
    }
    persister_ptr_->SaveRaftState(serializer.Bytes(), serializer.Size());
}


void Raft::ReadPersist() {
    if (persister_ptr_ == nullptr) {
        return;
    }
    auto data_vec = persister_ptr_->ReadRaftState();
    Serializer serializer(data_vec.data(), int(data_vec.size()));
    serializer.Decode(current_term_);
    serializer.Decode(vote_for_);
    serializer.Decode(commit_index_);
    serializer.Decode(last_applied_);
    int n_logs = 0;
    serializer.Decode(n_logs);
    logs_.resize(n_logs);
    for (int i = 0; i < n_logs; i ++) {
        serializer.DecodeObj(logs_[i]);
    }
//    spdlog::debug("结点[{}]读取持久化数据后的状态: ", name_);
//    spdlog::debug("\t\t\t\t term={}, vote_for={}, commit_index={}, last_applied={}, n_logs={}", current_term_, vote_for_, commit_index_, last_applied_, n_logs);
//    for (int i = 0; i < n_logs; i++) {
//        spdlog::debug("\t\t\t\t logs[{}]={}", i, logs_[i].String());
//    }
}


int Raft::SendCount() const {
    return rpc_count_.load();
}


bool Raft::IsRpcServerRunning() const {
    return rpc_server_->IsRunning();
}


Raft::~Raft() {
    std::unique_lock<std::mutex> lock(mu_);
    is_running_ = false;

    election_timeout_trigger_.Stop();
    replicate_cycle_timer_.Stop();
    apply_cycle_timer_.Stop();

    spdlog::debug("结点[{}]正在退出... ", name_);
}


void Raft::SetUp() {
    _installRpcService();           // 先注册一下RPC服务
    _installReceiveHandler();       // 确定收到数据包的解析、流程
    OpenService();                  // 打开自己的Rpc服务器
    _installTimers();               // 设置计时器的行为和倒计时
}

void Raft::_installTimers() {

    election_timeout_trigger_.SetUpTimeout(election_timeout_);
    auto raft_ptr = shared_from_this();
    auto election_callback = [raft_ptr] { raft_ptr->_electionHandler(); };
    election_timeout_trigger_.SetUpCallback(election_callback);

    replicate_cycle_timer_.SetUpTimeout(heart_beat_timeout_);
    auto replicate_callback = [raft_ptr] { raft_ptr->_replicateHandler(); };
    replicate_cycle_timer_.SetUpCallback(replicate_callback);

    apply_cycle_timer_.SetUpTimeout(ApplyInterval);
    auto apply_callback = [raft_ptr] { raft_ptr->_applyHandler(); };
    apply_cycle_timer_.SetUpCallback(apply_callback);
    spdlog::debug("结点[{}]初始化计时器完毕", name_);
}


void Raft::StartTimers(bool enable_candidate) {
    if (enable_candidate) {
        election_timeout_trigger_.Start();
    }
    apply_cycle_timer_.Start();
}




void Raft::AddPeer(const std::string &name, const PeerInfo &peer) {
    std::unique_lock<std::mutex> lock(mu_);
    if (name == name_) {
        return;
    }
    if (peers_.find(name) == peers_.end()) {
        peers_num_ ++;
    }
    CommsCentre::AddPeer(name, peer);
    if (status_ == RaftStatus::Leader) {
        next_index_.insert({name, int(logs_.size())});
    } else {
        next_index_.insert({name, 0});
    }
    match_index_.insert({name, 0});

}


std::tuple<int, int, bool> Raft::Start(std::string msg) {
    int index = -1;

    std::unique_lock<std::mutex> lock(mu_);
    bool is_leader = ( status_ == RaftStatus::Leader );
    int term = current_term_;

    if (is_leader) {
        spdlog::info("领导者[{}]收到客户端一条命令: [{}]", name_, msg);
        index = logs_.back().index + 1;
        logs_.emplace_back(LogEntry{index, term, msg});
        Persist();
    }
    return { index, term, is_leader };
}

std::pair<bool, std::string> Raft::IsCommitted(int index) {
    std::pair<bool, std::string> result {false, ""};
    std::unique_lock<std::mutex> lock(mu_);
    if (index >= int(logs_.size()) || index > commit_index_) {
        return result;
    }
    result.first = true;
    result.second = logs_[index].command;
    return result;
}

void Raft::Kill() {
    dead_.store(true);
    std::unique_lock<std::mutex> lock(mu_);
    replicate_cycle_timer_.Stop();
    election_timeout_trigger_.Stop();
    apply_cycle_timer_.Stop();
}

void Raft::Recover() {
    dead_.store(false);
    std::unique_lock<std::mutex> lock(mu_);
    if (status_ == RaftStatus::Leader) {
        replicate_cycle_timer_.Start();
    } else {
        election_timeout_trigger_.Start();
    }
    apply_cycle_timer_.Start();
}

bool Raft::Killed() {
    return dead_.load();
}

void Raft::SetLongDelay(bool long_delay) {
    std::unique_lock<std::mutex> lock(mu_);
    long_delays_ = long_delay;
}

void Raft::SetSendReliable(bool reliable) {
    std::unique_lock<std::mutex> lock(mu_);
    delay_sending_ = reliable;
}

void Raft::SetReplyReliable(bool reliable) {
    std::unique_lock<std::mutex> lock(mu_);
    delay_replying_ = reliable;
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

    Persist();
}

/// 这个函数的内容本来应该放在_toCandidate()内部
/// 结点变成Candidate之后, RequestVotes还没发完
/// 又超时了
/// 所以改变策略, 等发完了再执行这个函数的操作
void Raft::_restartElectionTimeout(int wait_time) {
    // 启动一个新的election_timeout_trigger_
    election_timeout_trigger_.Stop();
    election_timeout_ = GetRandomInt(ElectionTimeoutMin, ElectionTimeoutMax);
    election_timeout_ += wait_time;
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
    Persist();
//    _restartElectionTimeout();
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
        auto raft_ptr = shared_from_this();
        std::thread th_send([raft_ptr, args, sender, dst](){
            std::string message = args.Serialization();
            std::string reply_msg;
            spdlog::debug("结点[{}]向结点[{}]发送RequestVotesRPC请求, 数据包序列化后的内容「 {}」", raft_ptr->name_, dst, message);
            if (sender == nullptr) {
                return;
            }
            raft_ptr->rpc_count_.fetch_add(1, std::memory_order_release);
            if (!sender->SendMsg(message)) {
                return;
            }
            if (!sender->RecvMsg(&reply_msg)) {
                return;
            }

            spdlog::debug("结点[{}]收到结点[{}]的投票回复, 数据包序列化后的内容「 {}」", raft_ptr->name_, dst, reply_msg);
            RequestVoteReply reply;
            bool ok = RequestVoteReply::UnSerialization(reply_msg, reply);
            if (!ok) {
                spdlog::error("结点[{}]收到结点[{}]的投票回复内容解析出错", raft_ptr->name_, dst);
            } else {
                std::unique_lock<std::mutex> lock(raft_ptr->mu_);
                if (reply.term > raft_ptr->current_term_) {
                    spdlog::debug("结点[{}]的任期号{}, 结点[{}]的任期号{}, 竞选失败", raft_ptr->name_, raft_ptr->current_term_, dst, reply.term);
                    raft_ptr->_toFollower();
                    raft_ptr->_restartElectionTimeout();
                } else if (reply.vote_granted) {
                    if (raft_ptr->status_ == RaftStatus::Leader) {
                        spdlog::debug("结点[{}]收到结点[{}]的肯定投票, 不过已经是领导者了", raft_ptr->name_, dst);
                        return;
                    }
                    raft_ptr->votes_to_me_ ++;
                    spdlog::debug("结点[{}]收到结点[{}]的肯定投票, 现在有{}票, 一共有{}个结点", raft_ptr->name_, dst, raft_ptr->votes_to_me_, raft_ptr->peers_num_);
                    if (raft_ptr->votes_to_me_ * 2 > raft_ptr->peers_num_) {
                        spdlog::debug("结点[{}]收到多数派投票, 成为领导者, 任期号{}", raft_ptr->name_, raft_ptr->current_term_);
                        raft_ptr->_toLeader();
                    }
                } else {
                    spdlog::debug("结点[{}]竞选失败, err_msg= {}", raft_ptr->name_, reply.err_msg);
                }
            }

        });
        th_send.detach();
    }

    lock.lock();
    _restartElectionTimeout();
}

// time to replicate
void Raft::_replicateHandler() {
    std::unique_lock<std::mutex> lock(mu_);
    // 有没有必要检查一下身份？
    if (status_ != RaftStatus::Leader) {
        spdlog::debug("结点[{}]准备发送AppendEntries, 但是临时发现不是领导者", name_);
        return;
    }

    if (!delay_sending_) { // 随机睡眠一段时间再发心跳包
        int ms = 0;
        if (long_delays_) {
            ms = GetRandomInt(long_delay_min_, long_delay_max_);
        } else {
            ms = GetRandomInt(normal_delay_min_, normal_delay_max_);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
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
            auto raft_ptr = shared_from_this();
            lock.unlock();
            send_threads[i] = std::thread([raft_ptr, sender, args, dst](){ // 心跳包直接发送args就可以了
                std::string message = args.Serialization();
                spdlog::debug("结点[{}]向结点[{}]发送心跳包......", raft_ptr->name_, dst);
                raft_ptr->rpc_count_.fetch_add(1, std::memory_order_release);
                if (!sender->SendMsg(message)) {
                    spdlog::debug("结点[{}]向结点[{}]发送心跳包失败", raft_ptr->name_, dst);
                    return;
                }
                spdlog::debug("结点[{}]向结点[{}]发送心跳包, 数据内容: [{}]", raft_ptr->name_, dst, args.Serialization());
                std::string reply_msg;
                if (!sender->RecvMsg(&reply_msg)) {
                    return;
                }
                // 使用std::future::wait_for等待1秒钟

                spdlog::debug("结点[{}]收到结点[{}]的心跳包回复", raft_ptr->name_, dst);
                std::unique_lock<std::mutex> lock(raft_ptr->mu_);
                AppendEntriesReply reply;
                bool ok = AppendEntriesReply::UnSerialization(reply_msg, reply);
                if (!ok) {
                    spdlog::error("结点[{}]收到结点[{}]的心跳包回复, 但是解析出错", raft_ptr->name_, dst);
                    return;
                }
                if (raft_ptr->status_ != RaftStatus::Leader) {
                    spdlog::debug("结点[{}]收到结点[{}]的心跳包回复, 但是此时已经不是领导者", raft_ptr->name_, dst);
                    return;
                }
                spdlog::debug("结点[{}]收到结点[{}]的心跳包回复, 回复内容: 「 {}」", raft_ptr->name_, dst, reply.String());
                if (reply.term > raft_ptr->current_term_) {
                    spdlog::debug("结点[{}]收到结点[{}]的心跳包回复, 发现自己过期, {}<{}", raft_ptr->name_, dst, raft_ptr->current_term_, reply.term);
                    raft_ptr->current_term_ = reply.term;
                    raft_ptr->_toFollower();
                    raft_ptr->_restartElectionTimeout();
                    return;
                }
            });
        } else {
            const LogEntry & prev_log = logs_[next_index - 1];
            spdlog::debug("结点[{}]发给结点[{}]的AppendEntries上一条日志是[{}]", name_, peer_pipe.first, prev_log.String());
            int prev_log_index = prev_log.index;
            int prev_log_term = prev_log.term;
            std::string dst = peer_pipe.first;
            auto sender = peer_pipe.second;
            args.prev_log_index = prev_log_index;
            args.prev_log_term = prev_log_term;
            args.send_start_index = next_index;
            auto raft_ptr = shared_from_this();
            lock.unlock();
            send_threads[i] = std::thread([raft_ptr, dst, sender, args](){
                raft_ptr->rpc_count_.fetch_add(1, std::memory_order_release);
                if (!sender->SendMsg(args.Serialization())) {
                    spdlog::debug("结点[{}]向结点[{}]发送AppendEntries失败", raft_ptr->name_, dst);
                    return;
                }
                std::string reply_msg;
                if (!sender->RecvMsg(&reply_msg)) {
                    return;
                } // 使用std::future::wait_for等待1秒钟

                std::unique_lock<std::mutex> lock(raft_ptr->mu_);
                AppendEntriesReply reply;
                bool ok = AppendEntriesReply::UnSerialization(reply_msg, reply);
                if (!ok) {
                    spdlog::error("AppendEntriesRPC收到错误的回复类型");
                    return;
                }
                if (raft_ptr->status_ != RaftStatus::Leader) {
                    return;
                }

                if (reply.term > raft_ptr->current_term_) {
                    spdlog::info("收到[{}]的AppendEntries回复, 发现自己过期[收到term= {}, 自己的term= {}]", reply.server_name, reply.term, raft_ptr->current_term_);
                    raft_ptr->current_term_ = reply.term;
                    raft_ptr->_toFollower();
                    raft_ptr->_restartElectionTimeout();
                    return;
                }

                if (reply.success) {
                    raft_ptr->match_index_[reply.server_name] = reply.finished_index;
                    raft_ptr->next_index_[reply.server_name] = reply.finished_index + 1;
                    spdlog::debug("领导者[{}]正在查看[{}]序号的日志是否可以被提交", raft_ptr->name_, reply.finished_index);
                    if (raft_ptr->_isEnableCommit(reply.finished_index)) {
                        spdlog::debug("领导者[{}]发现[{}]序号的日志已经可以被提交", raft_ptr->name_, reply.finished_index);
                        raft_ptr->commit_index_ = reply.finished_index;
                        // 持久化
                        raft_ptr->Persist();
                    }
                } else {
                    raft_ptr->next_index_[reply.server_name] = reply.conflict_index;
                }

            });
        }
    }
    for (i = 0; i < n_thread; i ++) {
        send_threads[i].detach();
    }
}

// time to apply
void Raft::_applyHandler() {
    std::unique_lock<std::mutex> lock(mu_);
    while (last_applied_ < commit_index_) {
        last_applied_ ++;
        // TODO: apply一条日志

        // 持久化
        Persist();
    }
}



void Raft::_installRpcService() {
    // TODO: 暂时不用RPC服务的做法
}

void Raft::_installReceiveHandler() {

    auto raft_ptr = shared_from_this();
    rpc_server_->HandleReceiveData([raft_ptr](const std::string &recv, std::string &reply){
        std::unique_lock<std::mutex> lock(raft_ptr->mu_);
        if (!raft_ptr->is_running_) {
            return;
        }
        if (raft_ptr->Killed()) { // 忽略收到的数据包, 模拟网络断开
            reply = "";
            return;
        }

        std::istringstream is(recv);
        std::string package_name;
        is >> package_name;

        if (raft_ptr->delay_replying_) {
            int ms = GetRandomInt(raft_ptr->reply_delay_min_, raft_ptr->reply_delay_max_);
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }


//        spdlog::debug("结点[{}]收到{}数据包", raft_ptr->name_, package_name);

        if (package_name == "RequestVotes") {
            RequestVoteReply vote_reply;
            bool analysis_success = raft_ptr->_requestVotes(is, vote_reply);
            if (!analysis_success) {
                spdlog::error("结点[{}]解析RequestVotes出错", raft_ptr->name_);
                reply = "[package error]";
            } else {
                reply = vote_reply.Serialization();
            }
        } else if (package_name == "AppendEntries") {
//            spdlog::debug("结点[{}]收到AppendEntries请求", raft_ptr->name_);
            AppendEntriesReply append_reply;
            bool analysis_success = raft_ptr->_appendEntries(is, append_reply);
            if (!analysis_success) {
                spdlog::error("结点[{}]解析AppendEntries出错", raft_ptr->name_);
                reply = "[package error]";
            } else {
                reply = append_reply.Serialization();
            }
        } else {
            spdlog::error("结点[{}]收到未知类型的数据包: {}", raft_ptr->name_, package_name);
            reply = "Unknown type RPC. ";
        }
    });
}

/* AppendEntriesRPC内容
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

/*  AppendEntriesReply内容
 *  std::string server_name;
 *  int term;
 *  bool success;
 *  int finished_index;
 *  int conflict_index;
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
            reply.server_name = name_;
            reply.term = current_term_;
            reply.success = false;
            reply.finished_index = -1;
            reply.conflict_index = -1;
            spdlog::debug("结点[{}]收到结点[{}]的AppendEntriesRPC, 发现结点[{}]term过期", name_, leader_name, term);
            Persist();
            return true;  // 解析成功
        }

        reply.server_name = name_;
        current_term_ = term;
        _toFollower();
        reply.term = term;

        if (prev_log_index >= logs_.size()) {
            reply.conflict_index = int(logs_.size());
            spdlog::debug("结点[{}]收到结点[{}]的AppendEntriesRPC, 日志有冲突: 'prev_log_index >= logs_.size()'", name_, leader_name);
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
            spdlog::debug("结点[{}]收到结点[{}]的AppendEntriesRPC, 日志有冲突: 'prev_log_term != logs_[prev_log_index].term'", name_, leader_name);
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

            if (leader_committed_index > commit_index_) {
                commit_index_ = std::min(leader_committed_index, int(logs_.size())-1);
            }
            Persist();
            spdlog::debug("结点[{}]收到结点[{}]的AppendEntriesRPC, 日志匹配成功, 当前本结点日志数量[{}], committed_index=[{}], finished_index=[{}]", name_, leader_name, logs_.size(), commit_index_, reply.finished_index);
            election_timeout_trigger_.Reset();
        }

    } catch (const std::exception &e) {
        spdlog::error("{}", e.what());
        _restartElectionTimeout();
        return false;
    }

    _restartElectionTimeout();
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

        if (current_term_ > term || ( current_term_ == term && vote_for_ != "None" ) ) {
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
                _restartElectionTimeout();
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
        _restartElectionTimeout();

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
        int count = 1;  // 这里要从1开始, match_index_里面没有存储自己这个结点
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