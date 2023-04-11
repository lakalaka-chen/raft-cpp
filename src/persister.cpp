#include "persister.h"


namespace raft {

PersisterPtr Persister::MakePersister() {
    return PersisterPtr(new Persister);
}

PersisterPtr Persister::Copy() {
    std::lock_guard<std::mutex> lock(mu_);
    auto np = MakePersister();
    np->raft_state_.assign(raft_state_.begin(), raft_state_.end());
    np->snapshot_.assign(snapshot_.begin(), snapshot_.end());
    return np;
}


void Persister::SaveRaftState(const char *data, int len) {
    std::lock_guard<std::mutex> lock(mu_);
    raft_state_.assign(data, data+len);
}

int Persister::RaftStateSize() {
    std::lock_guard<std::mutex> lock(mu_);
    return int(raft_state_.size());
}

const std::vector<char>& Persister::ReadRaftState() {
    std::lock_guard<std::mutex> lock(mu_);
    return raft_state_;
}

void Persister::SaveSnapshot(char *data, int len) {
    std::lock_guard<std::mutex> lock(mu_);
    snapshot_.assign(data, data+len);
}

int Persister::SnapshotSize() {
    std::lock_guard<std::mutex> lock(mu_);
    return int(snapshot_.size());
}

const std::vector<char>& Persister::ReadSnapshot() {
    std::lock_guard<std::mutex> lock(mu_);
    return snapshot_;
}



}