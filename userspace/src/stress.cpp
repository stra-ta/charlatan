#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "charlatan.h"

namespace {

constexpr auto kDuration = std::chrono::seconds(2);

class FileDescriptor {
public:
    explicit FileDescriptor(int value) : value_(value) {}
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    ~FileDescriptor() {
        if (value_ >= 0) {
            (void)::close(value_);
        }
    }
    [[nodiscard]] int get() const { return value_; }

private:
    int value_;
};

FileDescriptor open_device() {
    return FileDescriptor(::open("/dev/charlatan0", O_RDWR | O_NONBLOCK | O_CLOEXEC));
}

bool reset_device(int fd) {
    if (::ioctl(fd, CHARLATAN_IOC_RESET) != 0) {
        return false;
    }
    charlatan_event ignored{};
    (void)::read(fd, &ignored, sizeof(ignored));
    return true;
}

}  // namespace

int main() {
    const FileDescriptor control = open_device();
    if (control.get() < 0) {
        std::perror("open /dev/charlatan0");
        return 1;
    }
    if (!reset_device(control.get())) {
        std::perror("reset");
        return 1;
    }
    charlatan_fault_config faults{.abi_version = CHARLATAN_ABI_VERSION, .fail_next_reads = 0,
                                  .fail_next_writes = 0, .producer_control_delay_ms = 0};
    if (::ioctl(control.get(), CHARLATAN_IOC_SET_FAULTS, &faults) != 0) {
        std::perror("clear faults");
        return 1;
    }
    charlatan_rate_config rate{.abi_version = CHARLATAN_ABI_VERSION, .period_ms = 1};
    if (::ioctl(control.get(), CHARLATAN_IOC_SET_RATE, &rate) != 0) {
        std::perror("start producer");
        return 1;
    }
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> writes{0};
    std::atomic<std::uint64_t> reads{0};
    std::atomic<std::uint64_t> resets{0};
    std::atomic<std::uint64_t> unexpected{0};

    auto writer = std::thread([&] {
        const FileDescriptor fd = open_device();
        if (fd.get() < 0) {
            ++unexpected;
            return;
        }
        charlatan_event event{};
        while (!stop.load(std::memory_order_relaxed)) {
            const auto result = ::write(fd.get(), &event, sizeof(event));
            if (result == static_cast<ssize_t>(sizeof(event))) {
                ++writes;
            } else if (result < 0 && errno != ENOSPC) {
                ++unexpected;
            } else if (result >= 0) {
                ++unexpected;
            }
        }
    });

    auto consumer = [&] {
        const FileDescriptor fd = open_device();
        if (fd.get() < 0) {
            ++unexpected;
            return;
        }
        charlatan_event event{};
        while (!stop.load(std::memory_order_relaxed)) {
            const auto result = ::read(fd.get(), &event, sizeof(event));
            if (result == static_cast<ssize_t>(sizeof(event))) {
                ++reads;
            } else if (result < 0 && errno != EAGAIN && errno != ECANCELED) {
                ++unexpected;
            } else if (result >= 0) {
                ++unexpected;
            }
        }
    };
    std::thread consumer_one(consumer);
    std::thread consumer_two(consumer);

    auto resetter = std::thread([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            if (::ioctl(control.get(), CHARLATAN_IOC_RESET) == 0) {
                ++resets;
            } else {
                ++unexpected;
            }
        }
    });

    auto opener = std::thread([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            const FileDescriptor fd = open_device();
            if (fd.get() >= 0) {
            } else {
                ++unexpected;
            }
        }
    });

    std::this_thread::sleep_for(kDuration);
    stop.store(true, std::memory_order_relaxed);
    writer.join();
    consumer_one.join();
    consumer_two.join();
    resetter.join();
    opener.join();

    if (::ioctl(control.get(), CHARLATAN_IOC_PAUSE) != 0) {
        ++unexpected;
    }
    std::cout << "writes=" << writes << " reads=" << reads << " resets=" << resets
              << " unexpected=" << unexpected << '\n';
    return unexpected == 0 ? 0 : 1;
}
