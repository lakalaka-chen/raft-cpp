#pragma once

#include "comms_centre.h"
#include "log_entry.h"
#include "append_entries.h"
#include "request_votes.h"
#include "time_utils.h"
#include "persister.h"
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
    static const int ElectionTimeoutMin = 200;
    static const int ElectionTimeoutMax = 400;
    static const int HeartBeatInterval = 80;
    static const int ApplyInterval = 50;    // 隔多少时间去尝试把committed log提交到状态机
    static RaftPtr Make(
            const std::vector<PeerInfo> &peers_info,
            const std::vector<std::string> &peers_name,
            const std::string &me, uint16_t port,
            PersisterPtr persister = nullptr);
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

    // 持久化
    PersisterPtr persister_ptr_;

    std::atomic<int> rpc_count_;

public:
    explicit Raft(std::string name, uint16_t port, PersisterPtr persister=nullptr);
    ~Raft() override;

    // 注册RPC服务
    // 确定收到数据包的解析、流程
    // 打开自己的Rpc服务器
    // 设置计时器的行为和倒计时
    void SetUp();

    // 启动apply计时器
    // 如果enable_candidate为true, 也会启动election timeout计时器
    void StartTimers(bool enable_candidate=true);

    // 添加一个结点信息, 但是不会主动发起连接
    void AddPeer(const std::string &name, const PeerInfo &peer) override;

    // 客户端向Raft结点发起一个请求
    // 返回值: index, term, is_leader
    std::tuple<int,int,bool> Start(std::string msg);

    // 查询index号日志有没有被commit
    std::pair<bool, std::string> IsCommitted(int index);

    // 关闭所有计时器
    // 不会再发送数据, 收到数据也不会再回复
    // 模拟结点宕机
    void Kill();
    // 恢复正常运行
    // 保持Kill之前的行为; 例如如果之前是Leader, 则恢复后会定时发送心跳; 如果是其他类型结点会开启election timeout计时器; 等等
    void Recover();
    bool Killed();
    void SetLongDelay(bool long_delay);
    void SetSendReliable(bool reliable);
    void SetReplyReliable(bool reliable);

    // 获取结点状态
    // 返回值: current_term_, is_leader
    std::pair<int ,bool> GetState();
    std::string GetName() { return name_; }

    // 持久化, 保存Raft结点信息
    void Persist();
    // 读取持久化数据
    void ReadPersist();

    // 返回目前该结点发送了多少个RPC, 暂时只有AppendEntriesRPC和RequestVotesRPC
    int SendCount() const;



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