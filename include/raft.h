#pragma once

#include "comms_centre.h"
#include "log_entry.h"
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


class Raft: public CommsCentre {
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
    int election_timeout_;  // 单位: ms
    TimePoint last_recv_time_;
    int heart_beat_timeout_; // 单位: ms
    TimePoint last_heart_beat_time_;


    TriggerTimer election_timeout_trigger_;  // 处理ElectionTimeOut
    CycleTimer replicate_cycle_timer_;         // 处理日志复制, 日志复制和心跳包一起处理, 超时时间按心跳时间设定
    CycleTimer apply_cycle_timer_;             // 执行到状态机触发器

public:
    explicit Raft(std::string name, uint16_t port);
    ~Raft();
    std::tuple<int,int,bool> Start(const std::string &msg);
    void Kill();
    bool Killed();
    std::pair<int ,bool> GetState() const;
//    void Persist();
//    void ReadPersist();


protected:
    void _toCandidate();
    void _toFollower();
    void _toLeader();

//    void _electionMonitor();
//    void _replicateMonitor();
//    void _applyMonitor();

    void _electionHandler();
    void _replicateHandler();
    void _applyHandler();



};



}