#pragma once

#include <mutex>
#include <condition_variable>
#include <queue>

#define CHAN_TEMPLATE template<typename ElemType>

namespace utility {


template <typename ElemType>
class Chan {
public:
    Chan(const Chan<ElemType> &) = delete;
    Chan<ElemType> & operator=(const Chan<ElemType>&) = delete;

    explicit Chan(int capacity=1);

    void operator << (const ElemType &product);
    void operator >> (ElemType &output);

private:
    std::mutex mu_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::queue<ElemType> queue_;
    int capacity_;
};


CHAN_TEMPLATE
Chan<ElemType> MakeChan(int capacity=1) {
    return Chan<ElemType>(capacity);
}

CHAN_TEMPLATE
void Chan<ElemType>::operator << (const ElemType &product) {
    std::unique_lock<std::mutex> lock(mu_);
    while (queue_.size() == capacity_) {
        not_full_.wait(lock);
        break;
    }
    assert ( queue_.size() < capacity_ );
    if (queue_.empty()) {
        /*
         * 为什么只有queue_为空的时候需要notify_one？
         * 我是借鉴LevelDB的设计
         * 如果queue_是空的, 意味着可能有线程在等待数据, 于是producer在添加数据后唤醒一个consumer
         * 如果queue_不是空的, 意味着肯定没有线程在等待数据; 不需要notify任何
         * */
        not_empty_.notify_one();
    }
    queue_.push(product);
}

CHAN_TEMPLATE
void Chan<ElemType>::operator >> (ElemType &output) {
    std::unique_lock<std::mutex> lock(mu_);
    while (queue_.empty()) {
        not_empty_.wait(lock);
        break;
    }
    assert ( !queue_.empty() );
    if (queue_.size() == capacity_) {
        // 思路同上
        not_full_.notify_one();
    }
    output = queue_.front(); queue_.pop();
}


CHAN_TEMPLATE
Chan<ElemType>::Chan(int capacity)
        : capacity_(capacity) {

}

}