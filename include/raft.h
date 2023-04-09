#pragma once

#include "comms_centre.h"
#include "log_entry.h"
#include "append_entries.h"
#include "request_votes.h"
#include "time_utils.h"
#include "timer/trigger_timer.h"

#include <memory>
#include <chrono>

namespace raft {

enum RaftStatus {
    Leader = 1,
    Candidate = 2,
    Follower = 3,
};

class Raft;
using RaftPtr = std::shared_ptr<Raft>;

using trigger_timer::TriggerTimer;
using trigger_timer::CycleTimer;
//using trigger_timer::TimePoint;


class Raft: public CommsCentre, public std::enable_shared_from_this<Raft> {
public:
    static const int ElectionTimeoutMin = 550;
    static const int ElectionTimeoutMax = 700;
    static const int HeartBeatInterval = 150;
    static const int ApplyInterval = 50;    // 隔多少时间去尝试把committed log提交到状态机
    static RaftPtr Make(
            const std::vector<PeerInfo> &peers_info,
            const std::vector<std::string> &peers_name,
            const std::string &me, uint16_t port);
private:
    std::mutex mu_;
    bool is_running_;

    int peers_num_;

    int current_term_;              // 已知最新term, 初始化为0
    std::string vote_for_;          // 上一轮投票给谁, 初始化为"None"
    std::vector<LogEntry> logs_;
    int commit_index_;              // 本结点已经committed日志最大index
    int last_applied_;              // 本结点已经更新到状态机的日志最大index
    RaftStatus status_;

    std::unordered_map<std::string, int> next_index_;
    std::unordered_map<std::string, int> match_index_;

    std::atomic<bool> dead_;
    int votes_to_me_;

    /// 处理时间的变量
    int election_timeout_;              // 单位: ms
    int heart_beat_timeout_;            // 单位: ms


    TriggerTimer election_timeout_trigger_;  // 处理ElectionTimeOut
    CycleTimer replicate_cycle_timer_;         // 处理日志复制, 日志复制和心跳包一起处理, 超时时间按心跳时间设定
    CycleTimer apply_cycle_timer_;             // 执行到状态机触发器


    // 模拟网络错误的成员变量
    bool delay_sending_{false};  // 延迟发送
    bool delay_replying_{false}; // 延迟回复


    // 发送延迟参数
    bool long_delays_{false};
    int normal_delay_min_{0};
    int normal_delay_max_{100};  // 单位: ms
    int long_delay_min_{0};
    int long_delay_max_{7000};   // 单位: ms

    // 回复延迟参数
    int reply_delay_min_{200};
    int reply_delay_max_{2200};

public:
    explicit Raft(std::string name, uint16_t port);
    ~Raft() override;

    void SetUp();

    void StartTimers(bool enable_candidate=true);   // 启动计时器
    void AddPeer(const std::string &name, const PeerInfo &peer) override;

    /// 返回值: index, term, is_leader
    std::tuple<int,int,bool> Start(std::string msg);
    std::pair<bool, std::string> IsCommitted(int index);

    void Kill();
    void Recover();
    bool Killed();
    void SetLongDelay(bool long_delay);
    void SetSendReliable(bool reliable);
    void SetReplyReliable(bool reliable);
    std::pair<int ,bool> GetState();
    std::string GetName() { return name_; }
//    void Persist();
//    void ReadPersist();



protected:
    void _toCandidate();
    void _toFollower();
    void _toLeader();

    void _restartElectionTimeout();


    void _electionHandler();
    void _replicateHandler();
    void _applyHandler();


    void _installTimers();          // 设置计时器行为、倒计时
    void _installRpcService();
    void _installReceiveHandler();

    bool _appendEntries(std::istringstream &is, AppendEntriesReply &reply);
    bool _requestVotes(std::istringstream &is, RequestVoteReply &reply);

    bool _isEnableCommit(int index);

};



}