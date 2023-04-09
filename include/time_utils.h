#pragma once

#include <chrono>
#include <random>

namespace raft {

using TimePoint = std::chrono::steady_clock::time_point;
using Clock = std::chrono::steady_clock;
using DurationUnit = std::chrono::milliseconds;


int GetRandomInt(int min, int max);

DurationUnit::rep CalcDuration(TimePoint end, TimePoint start);

}