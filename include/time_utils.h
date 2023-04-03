
#include <chrono>
#include <random>

namespace raft {

using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
using DurationUnit = std::chrono::milliseconds;
using Clock = std::chrono::steady_clock;


int GetRandDuration(int min, int max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(min, max);
    return dis(gen);
}

//DurationUnit GetRandDuration(int min, int max) {
//    return DurationUnit(GetRandom(min, max));
//}


}