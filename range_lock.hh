/*
 * Copyright (C) 2016 Raphael S. Carvalho
 *
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
 */

#pragma once

#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cmath>
#include <assert.h>
#if (__cplusplus >= 201402L)
#include <shared_mutex>
#else
#include <mutex>
#endif


/// \brief Range lock class
///
/// Utility created to control access to specific regions of a shared resource,
/// such as a buffer or file. Think of it as byte-range locking mechanism.
///
/// This works by dividing the resource into N regions, and associating an id
/// with each region. A lower granularity results in finer-grained locking.

/// A lock request may cover more than one region, so there is a need to wait
/// for each region before returning to the user. To avoid deadlock, regions
/// are always locked sequentially, thus technically the same order.
///
/// This implementation is resource efficient because it will only keep alive
/// data for the regions being used at the moment. That's done with a simple
/// reference count management.
struct range_lock {
private:
    struct entry {
        uint64_t refcount = 0;
#if (__cplusplus >= 201402L)
        std::shared_timed_mutex mutex;
#else
        std::mutex mutex;
#endif
    };
    std::unordered_map<uint64_t, std::unique_ptr<entry>> _entries;
    std::mutex _entries_lock;
    const uint64_t _granularity;
public:
    range_lock() = delete;
    range_lock(uint64_t granularity) : _granularity(granularity) {
        /// assert that granularity is greater than 0 and power of 2.
        assert(granularity > 0);
        assert((granularity & (granularity - 1)) == 0);
    }
    // Create range_lock with a good granularity given the size of a resource
    static std::unique_ptr<range_lock> create_range_lock(uint64_t resource_size) {
        auto res = ceil(std::log2(resource_size) * 0.5);
        auto exp = std::max(uint64_t(res), uint64_t(10));
        uint64_t granularity = uint64_t(pow(2, exp));
        return static_cast<std::unique_ptr<range_lock>>(new range_lock(granularity));
    }
private:
    entry& get_locked_entry(uint64_t entry_id) {
        std::lock_guard<std::mutex> lock(_entries_lock);
        auto it = _entries.find(entry_id);
        assert(it != _entries.end()); // assert entry exists.
        entry& e = *(it->second);
        assert(e.refcount > 0); // assert entry is locked.
        return e;
    }

    entry& get_and_lock_entry(uint64_t entry_id) {
        std::lock_guard<std::mutex> lock(_entries_lock);
        auto it = _entries.find(entry_id);
        if (it == _entries.end()) {
            std::unique_ptr<entry> e(new entry);
            auto ret = _entries.insert(std::make_pair(entry_id, std::move(e)));
            it = ret.first;
        }
        entry& e = *(it->second);
        e.refcount++;
        return e;
    }

    void unlock_entry(uint64_t entry_id) {
        std::lock_guard<std::mutex> lock(_entries_lock);
        auto it = _entries.find(entry_id);
        assert(it != _entries.end());
        entry& e = *(it->second);
        if (--e.refcount == 0) {
            _entries.erase(it);
        }
    }

    inline uint64_t get_entry_id(uint64_t offset) {
        return offset / _granularity;
    }

    inline void
    do_for_each_entry_id(uint64_t offset, uint64_t length, std::function<void(uint64_t)> f) {
        auto assert_alignment = [] (uint64_t v, uint64_t a) {
            assert((v & (a - 1)) == 0);
        };
        assert_alignment(offset, _granularity);
        assert_alignment(length, _granularity);
        auto entries = length / _granularity;
        assert(length % _granularity == 0);
        for (auto i = 0; i < entries; i++) {
            auto current_offset = offset + (i * _granularity);
            auto entry_id = get_entry_id(current_offset);
            f(entry_id);
        }
    }

    void for_each_entry_id(uint64_t offset, uint64_t length, std::function<void(uint64_t)> f) {
        uint64_t aligned_down_offset = offset & ~(_granularity - 1);
        uint64_t aligned_up_length = (length + _granularity) & ~(_granularity - 1);
        do_for_each_entry_id(aligned_down_offset, aligned_up_length, std::move(f));
    }

    void validate_parameters(uint64_t offset, uint64_t length) {
        assert(offset < (offset + length)); // check for overflow
        assert(length > 0);
    }
public:
    uint64_t granularity() const { return _granularity; }

    // Lock [offset, offset+length) for exclusive ownership.
    void lock(uint64_t offset, uint64_t length) {
        validate_parameters(offset, length);
        for_each_entry_id(offset, length, [this] (uint64_t entry_id) {
            entry& e = this->get_and_lock_entry(entry_id);
            e.mutex.lock();
        });
    }

    // Unlock [offset, offset+length)
    void unlock(uint64_t offset, uint64_t length) {
        validate_parameters(offset, length);
        for_each_entry_id(offset, length, [this] (uint64_t entry_id) {
            entry& e = this->get_locked_entry(entry_id);
            e.mutex.unlock();
            this->unlock_entry(entry_id);
        });
    }

    // Execute an operation with range [offset, offset+length) locked for exclusive ownership.
    template <typename Func>
    void with_lock(uint64_t offset, uint64_t length, Func&& func) {
        lock(offset, length);
        func();
        unlock(offset, length);
    }

#if (__cplusplus >= 201402L)
    // Lock [offset, offset+length) for shared ownership.
    void lock_shared(uint64_t offset, uint64_t length) {
        validate_parameters(offset, length);
        for_each_entry_id(offset, length, [this] (uint64_t entry_id) {
            entry& e = this->get_and_lock_entry(entry_id);
            e.mutex.lock_shared();
        });
    }

    // Unlock [offset, offset+length)
    void unlock_shared(uint64_t offset, uint64_t length) {
        validate_parameters(offset, length);
        for_each_entry_id(offset, length, [this] (uint64_t entry_id) {
            entry& e = this->get_locked_entry(entry_id);
            e.mutex.unlock_shared();
            this->unlock_entry(entry_id);
        });
    }

    // Execute an operation with range [offset, offset+length) locked for shared ownership.
    template <typename Func>
    void with_lock_shared(uint64_t offset, uint64_t length, Func&& func) {
        lock_shared(offset, length);
        func();
        unlock_shared(offset, length);
    }
#endif
};
