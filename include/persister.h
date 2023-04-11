#pragma once

#include <vector>
#include <mutex>
#include <memory>

namespace raft {

class Persister;
using PersisterPtr = std::shared_ptr<Persister>;

class Persister {
private:
    std::mutex mu_;
    std::vector<char> raft_state_;
    std::vector<char> snapshot_;
    Persister() = default;


public:
    static PersisterPtr MakePersister();
    PersisterPtr Copy();
    ~Persister() = default;

    Persister(const Persister &) = delete;
    Persister & operator=(const Persister&) = delete;


    void SaveRaftState(const char *data, int len);
    int RaftStateSize();
    const std::vector<char>& ReadRaftState();

    void SaveSnapshot(char *data, int len);
    int SnapshotSize();
    const std::vector<char>& ReadSnapshot();


};


}