#include <iostream>
#include <thread>
#include "range_lock.hh"

int main(void) {
    auto range_lock = range_lock::create_range_lock(pow(2, 30));
    std::cout << "Range lock granularity: " << range_lock->granularity() << std::endl;

    auto t = std::thread([&range_lock] {
        std::cout << "Trying to lock [0, 1024)\n";
        range_lock->with_lock(0, 1024, [] {
            std::cout << "Locked [0, 1024) properly, sleeping for 1 second...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        });
        std::cout << "Unlocked [0, 1024) properly\n";
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Trying to lock [0, 1024*1024)\n";
    range_lock->with_lock(0, 1024*1024, [] {
        std::cout << "Locked [0, 1024*1024) properly\n";
    });
    std::cout << "Unlocked [0, 1024*1024) properly\n";

    t.join();

    return 0;
}
