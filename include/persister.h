#pragma once

#include <mutex>
#include <memory>

namespace raft {

//class Persister;
//using PersisterPtr = std::unique_ptr<Persister>;
//
//class Persister {
//private:
//    std::mutex mu_;
//    char * raft_state_{nullptr};
//    int raft_size_{0};
//    char * snapshot_{nullptr};
//    int snapshot_size_{0};
//    Persister() = default;
//
//public:
//    static PersisterPtr MakePersister();
//    PersisterPtr Copy();
//
//    void SaveRaftState(char *data, int len);
//    int RaftStateSize();
//    const char* ReadRaftState();
//
//    void SaveSnapshot(char *data, int len);
//    int SnapshotSize();
//    const char* Readnapshot();
//
//    Persister(const Persister &) = delete;
//    Persister & operator=(const Persister&) = delete;
//
//};


}