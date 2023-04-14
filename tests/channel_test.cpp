
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "channel.h"


TEST(ChannelTest, TEST1) {
    spdlog::set_level(spdlog::level::debug);
    utility::Chan<int> ch = utility::MakeChan<int>(2);
    int total_sends = 10;
    auto producer = [&ch, total_sends](){
        for (int i = 0; i < total_sends; i ++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            ch << 100+i;
            spdlog::debug("Producer enqueue {}", 100+i);
        }
    };

    auto consumer = [&ch, total_sends](){
        int value;
        for (int i = 0; i < total_sends; i ++) {
            ch >> value;
            spdlog::debug("Consumer get value {}", value);
        }
    };

    std::thread producer_thread(producer);
    std::thread consumer_thread(consumer);

    if (producer_thread.joinable()) {
        producer_thread.join();
    }

    if (consumer_thread.joinable()) {
        consumer_thread.join();
    }
}
