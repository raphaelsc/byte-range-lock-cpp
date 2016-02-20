///
/// Purpose of this program is to test range_lock implementation.
///
/// Sincerely,
/// Raphael Carvalho
///

#include "range_lock.hh"
#include <iostream>
#include <thread>
#include <assert.h>

static inline void print_test_name(void) {
    std::cout << "\nRunning " << __FUNCTION__ << "...\n";
}

static void basic_range_lock_test(range_lock& range_lock) {
    print_test_name();

    auto t = std::thread([&range_lock] {
        std::cout << "Trying to lock [0, 1024)\n";
        range_lock.with_lock(0, 1024, [] {
            std::cout << "Locked [0, 1024) properly, sleeping for 2 seconds...\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
        });
        std::cout << "Unlocked [0, 1024) properly\n";
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Trying to lock [0, 1024*1024)\n";
    range_lock.with_lock(0, 1024*1024, [] {
        std::cout << "Locked [0, 1024*1024) properly\n";
    });
    std::cout << "Unlocked [0, 1024*1024) properly\n";

    t.join();
}

static void try_lock_test(range_lock& range_lock) {
    print_test_name();

    auto t = std::thread([&range_lock] {
        std::cout << "Trying to lock [4096, 8192)\n";
        range_lock.with_lock(4096, 8192, [] {
            std::cout << "Locked [4096, 8192) properly, sleeping for 2 second...\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
        });
        std::cout << "Unlocked [4096, 8192) properly\n";
    });

    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "Checking that [0, 8192) cannot be immediately acquired\n";
    bool acquired = range_lock.try_lock(0, 8192);
    assert(!acquired);
    std::cout << "Succeeded\n";

    t.join();

    std::cout << "Checking that [0, 8192) can be immediately acquired\n";
    acquired = range_lock.try_lock(0, 8192);
    assert(acquired);
    std::cout << "Succeeded\n";
}

int main(void) {
    auto range_lock = range_lock::create_range_lock(pow(2, 30));
    std::cout << "Range lock granularity (a.k.a. region size): " << range_lock->region_size() << std::endl;

    basic_range_lock_test(*range_lock);
    try_lock_test(*range_lock);

    return 0;
}
