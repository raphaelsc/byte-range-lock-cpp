#include <iostream>
#include "range_lock.hh"

int main(void) {
    auto range_lock = range_lock::create_range_lock(pow(2, 30));
    std::cout << range_lock->granularity() << std::endl;

    range_lock->with_lock(0, 1024*1024, [] {
        std::cout << "Locked properly\n";
    });
    std::cout << "Unlocked properly\n";

    return 0;
}