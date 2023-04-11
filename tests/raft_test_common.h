
#include "raft.h"

namespace raft {

/// 顾名思义
std::pair<bool, RaftPtr>
checkOneLeader(const std::vector<RaftPtr> & machines);

/// 测试所有结点的current_term_是不是一致的
bool
checkTermsSame(const std::vector<RaftPtr> & machines);

/// 多少个服务器认为index位置的日志已经committed
std::pair<int, std::string>
nCommitted(const std::vector<RaftPtr> & machines, int index);

/// 提交一条日志command
/// 并且期待有expectedServers提交成功
/// 返回command对应的索引
int
one(const std::vector<RaftPtr> & machines, const std::string &command, int expectedServers);


/// 顾名思义
RaftPtr
killOneFollower(const std::vector<RaftPtr> & machines);

void
killAllServers(const std::vector<RaftPtr> & machines);

void
recoverAllServers(const std::vector<RaftPtr> & machines);


/// 等待至少expectServers个服务器提交index号日志
/// 返回index处的command
/// 返回"-1"表示失败
std::string
configWait(const std::vector<RaftPtr> & machines, int index, int expectServers, int term);

}

