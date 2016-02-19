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
/// such as a buffer or a file. Think of it as byte-range locking mechanism.
///
/// This implementation works by virtually dividing the shared resource into N
/// regions of the same size, and associating an id with each region.
/// A region is the unit to be individually protected from concurrent access.
///
/// Choosing an optimal region size:
/// The smaller the region size, the more regions exists.
/// The more regions exists, the finer grained the locking is.
/// If in doubt, use range_lock::create_range_lock(). It will choose a region
/// size for you.
///
/// How locking does work with range_lock?
/// A lock request may cover more than one region, so there is a need to wait
/// for each covered region to be available. Deadlock is avoided by always
/// locking regions in sequential order.
///
/// This implementation is resource efficient because it will only keep alive
/// data for the regions being used at the moment. That's done with a simple
/// reference count management.
struct range_lock {
private:
    struct region {
        uint64_t refcount = 0;
#if (__cplusplus >= 201402L)
        std::shared_timed_mutex mutex;
#else
        std::mutex mutex;
#endif
    };
    std::unordered_map<uint64_t, std::unique_ptr<region>> _regions;
    std::mutex _regions_lock;
    const uint64_t _region_size;
public:
    range_lock() = delete;
    range_lock(uint64_t region_size) : _region_size(region_size) {
        /// assert that region_size is greater than 0 and power of 2.
        assert(region_size > 0);
        assert((region_size & (region_size - 1)) == 0);
    }
    // Create a range_lock with a region size, which is calculated based on the
    // size of resource to be protected.
    // For example, if you want to protect a file, call create_range_lock()
    // with the size of that file.
    static std::unique_ptr<range_lock> create_range_lock(uint64_t resource_size) {
        auto res = ceil(std::log2(resource_size) * 0.5);
        auto exp = std::max(uint64_t(res), uint64_t(10));
        uint64_t region_size = uint64_t(pow(2, exp));
        return static_cast<std::unique_ptr<range_lock>>(new range_lock(region_size));
    }
private:
    region& get_locked_region(uint64_t region_id) {
        std::lock_guard<std::mutex> lock(_regions_lock);
        auto it = _regions.find(region_id);
        assert(it != _regions.end()); // assert region exists
        region& r = *(it->second);
        assert(r.refcount > 0); // assert region is locked
        return r;
    }

    region& get_and_lock_region(uint64_t region_id) {
        std::lock_guard<std::mutex> lock(_regions_lock);
        auto it = _regions.find(region_id);
        if (it == _regions.end()) {
            std::unique_ptr<region> r(new region);
            auto ret = _regions.insert(std::make_pair(region_id, std::move(r)));
            it = ret.first;
        }
        region& r = *(it->second);
        r.refcount++;
        return r;
    }

    void unlock_region(uint64_t region_id) {
        std::lock_guard<std::mutex> lock(_regions_lock);
        auto it = _regions.find(region_id);
        assert(it != _regions.end());
        region& r = *(it->second);
        if (--r.refcount == 0) {
            _regions.erase(it);
        }
    }

    inline uint64_t get_region_id(uint64_t offset) {
        return offset / _region_size;
    }

    inline void
    do_for_each_region(uint64_t offset, uint64_t length, std::function<void(uint64_t)> f) {
        auto assert_alignment = [] (uint64_t v, uint64_t alignment) {
            assert((v & (alignment - 1)) == 0);
        };
        assert_alignment(offset, _region_size);
        assert_alignment(length, _region_size);
        auto regions = length / _region_size;
        assert(length % _region_size == 0);
        for (auto i = 0; i < regions; i++) {
            auto current_offset = offset + (i * _region_size);
            f(get_region_id(current_offset));
        }
    }

    void for_each_region(uint64_t offset, uint64_t length, std::function<void(uint64_t)> f) {
        uint64_t aligned_down_offset = offset & ~(_region_size - 1);
        uint64_t aligned_up_length = (length + _region_size - 1) & ~(_region_size - 1);
        do_for_each_region(aligned_down_offset, aligned_up_length, std::move(f));
    }

    void validate_parameters(uint64_t offset, uint64_t length) {
        assert(offset < (offset + length)); // check for overflow
        assert(length > 0);
    }
public:
    uint64_t region_size() const { return _region_size; }

    // Lock range [offset, offset+length) for exclusive ownership.
    void lock(uint64_t offset, uint64_t length) {
        validate_parameters(offset, length);
        for_each_region(offset, length, [this] (uint64_t region_id) {
            region& r = this->get_and_lock_region(region_id);
            r.mutex.lock();
        });
    }

    // Unlock range [offset, offset+length) from exclusive ownership.
    void unlock(uint64_t offset, uint64_t length) {
        validate_parameters(offset, length);
        for_each_region(offset, length, [this] (uint64_t region_id) {
            region& r = this->get_locked_region(region_id);
            r.mutex.unlock();
            this->unlock_region(region_id);
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
    // Lock range [offset, offset+length) for shared ownership.
    void lock_shared(uint64_t offset, uint64_t length) {
        validate_parameters(offset, length);
        for_each_region(offset, length, [this] (uint64_t region_id) {
            region& r = this->get_and_lock_region(region_id);
            r.mutex.lock_shared();
        });
    }

    // Unlock range [offset, offset+length) from shared ownership.
    void unlock_shared(uint64_t offset, uint64_t length) {
        validate_parameters(offset, length);
        for_each_region(offset, length, [this] (uint64_t region_id) {
            region& r = this->get_locked_region(region_id);
            r.mutex.unlock_shared();
            this->unlock_region(region_id);
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
