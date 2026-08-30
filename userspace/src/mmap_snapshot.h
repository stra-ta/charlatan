// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <system_error>

#include <sys/mman.h>
#include <unistd.h>

#include "charlatan.h"

namespace charlatan_test {

static_assert(sizeof(charlatan_mmap_snapshot) <= 4096,
              "mmap snapshot must fit in the minimum supported page");

struct Snapshot {
    std::uint64_t version = 0;
    std::uint64_t reset_generation = 0;
    std::uint64_t next_sequence = 0;
    std::uint32_t abi_version = 0;
    std::uint32_t event_size = 0;
    std::uint32_t queue_capacity = 0;
    std::uint32_t queue_depth = 0;
    std::array<charlatan_event, CHARLATAN_MMAP_SNAPSHOT_EVENTS> events{};
};

class MmapSnapshot {
public:
    explicit MmapSnapshot(int fd) : page_size_(static_cast<std::size_t>(::getpagesize())) {
        mapping_ = static_cast<const volatile charlatan_mmap_snapshot*>(
            ::mmap(nullptr, page_size_, PROT_READ, MAP_SHARED, fd, 0));
        if (mapping_ == MAP_FAILED) {
            mapping_ = nullptr;
            throw std::system_error(errno, std::generic_category(), "mmap snapshot");
        }
    }

    MmapSnapshot(const MmapSnapshot&) = delete;
    MmapSnapshot& operator=(const MmapSnapshot&) = delete;

    ~MmapSnapshot() {
        if (mapping_ != nullptr) {
            (void)::munmap(const_cast<charlatan_mmap_snapshot*>(mapping_), page_size_);
        }
    }

    [[nodiscard]] Snapshot read() const {
        for (unsigned attempt = 0; attempt < 1000; ++attempt) {
            const auto before = __atomic_load_n(&mapping_->version, __ATOMIC_ACQUIRE);
            if ((before & 1U) != 0) {
                continue;
            }

            std::atomic_thread_fence(std::memory_order_acquire);
            Snapshot copy{};
            copy.reset_generation = mapping_->reset_generation;
            copy.next_sequence = mapping_->next_sequence;
            copy.abi_version = mapping_->abi_version;
            copy.event_size = mapping_->event_size;
            copy.queue_capacity = mapping_->queue_capacity;
            copy.queue_depth = mapping_->queue_depth;
            for (std::size_t index = 0; index < copy.events.size(); ++index) {
                copy.events[index].sequence = mapping_->events[index].sequence;
                copy.events[index].produced_ns = mapping_->events[index].produced_ns;
                copy.events[index].value = mapping_->events[index].value;
                copy.events[index].flags = mapping_->events[index].flags;
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            const auto after = __atomic_load_n(&mapping_->version, __ATOMIC_ACQUIRE);
            if (before == after && (after & 1U) == 0) {
                copy.version = after;
                return copy;
            }
        }
        throw std::runtime_error("mmap snapshot remained unstable");
    }

private:
    std::size_t page_size_;
    const volatile charlatan_mmap_snapshot* mapping_ = nullptr;
};

}  // namespace charlatan_test
