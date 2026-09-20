#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace rush::run {
struct Split {
    std::string id, label;
    int64_t milliseconds;
    int item_id = -1;
};
// Times are monotonic milliseconds. UI refreshes never advance the clock.
class Record {
public:
    void begin(int64_t now) {
        started_ = now;
        stopped_ = 0;
        running_ = true;
        started_once_ = true;
        splits.clear();
        seen_.clear();
    }
    int64_t elapsed(int64_t now) const {
        if (!started_once_) {
            return 0;
        }
        return std::max<int64_t>(0, (running_ ? now : stopped_) - started_);
    }
    bool split(std::string id, std::string label, int64_t now) {
        if (!running_ || !seen_.insert(id).second) {
            return false;
        }
        splits.push_back({std::move(id), std::move(label), elapsed(now)});
        return true;
    }
    void finish(int64_t now) {
        if (running_) {
            stopped_ = now;
            running_ = false;
        }
    }
    bool running() const { return running_; }
    std::vector<Split> splits;

private:
    int64_t started_ = 0, stopped_ = 0;
    bool running_ = false, started_once_ = false;
    std::unordered_set<std::string> seen_;
};
}  // namespace rush::run
