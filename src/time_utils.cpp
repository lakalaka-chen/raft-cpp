#include "time_utils.h"

namespace raft {

int GetRandomInt(int min, int max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(min, max);
    return dis(gen);
}


DurationUnit::rep CalcDuration(TimePoint end, TimePoint start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();
}

}