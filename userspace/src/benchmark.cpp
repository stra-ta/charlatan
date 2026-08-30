// SPDX-License-Identifier: MIT
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
#include <string>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "charlatan.h"
#include "mmap_snapshot.h"

int main(int argc, char* argv[]) {
    const std::uint32_t events = argc == 2 ? static_cast<std::uint32_t>(std::stoul(argv[1])) : 10000;
    const std::string transport = argc == 3 ? argv[2] : "syscall";
    if (transport != "syscall" && transport != "mmap") {
        std::cerr << "usage: charlatan-bench [events] [syscall|mmap]\n";
        return 2;
    }
    const int fd = ::open("/dev/charlatan0", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        std::perror("open /dev/charlatan0");
        return 1;
    }
    if (::ioctl(fd, CHARLATAN_IOC_RESET) != 0) {
        std::perror("reset");
        return 1;
    }
    charlatan_event acknowledge{};
    (void)::read(fd, &acknowledge, sizeof(acknowledge));
    std::optional<charlatan_test::MmapSnapshot> snapshot;
    if (transport == "mmap") {
        snapshot.emplace(fd);
    }
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t index = 0; index < events; ++index) {
        charlatan_event event{.sequence = 0, .produced_ns = 0, .value = index, .flags = 0};
        if (::write(fd, &event, sizeof(event)) != static_cast<ssize_t>(sizeof(event))) {
            std::perror("stream operation");
            return 1;
        }
        if (snapshot.has_value()) {
            const auto view = snapshot->read();
            if (view.queue_depth == 0 || view.events[0].value != index) {
                std::cerr << "mmap snapshot did not expose the queued event\n";
                return 1;
            }
        }
        if (::read(fd, &event, sizeof(event)) != static_cast<ssize_t>(sizeof(event))) {
            std::perror("stream operation");
            return 1;
        }
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "{\"transport\":\"" << transport << "\",\"events\":" << events
              << ",\"seconds\":" << elapsed
              << ",\"events_per_second\":" << events / elapsed << "}\n";
    return 0;
}
