#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <unistd.h>

#include "charlatan.h"

namespace {

using namespace std::chrono_literals;

class FileDescriptor {
public:
    explicit FileDescriptor(int value) : value_(value) {
        if (value_ < 0) {
            throw std::system_error(errno, std::generic_category(), "open");
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : value_(std::exchange(other.value_, -1)) {}

    ~FileDescriptor() {
        if (value_ >= 0) {
            (void)::close(value_);
        }
    }

    [[nodiscard]] int get() const { return value_; }

private:
    int value_;
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

FileDescriptor open_device(int flags = O_RDWR) {
    return FileDescriptor(::open("/dev/charlatan0", flags | O_CLOEXEC));
}

void expect_errno(ssize_t result, int expected, const std::string& operation) {
    require(result == -1, operation + " unexpectedly succeeded");
    require(errno == expected, operation + " expected errno " + std::to_string(expected) +
                                  ", got " + std::to_string(errno));
}

void reset_and_acknowledge(int fd) {
    require(::ioctl(fd, CHARLATAN_IOC_RESET) == 0, "reset failed");
    charlatan_event event{};
    expect_errno(::read(fd, &event, sizeof(event)), ECANCELED, "reset acknowledgement");
}

void write_event(int fd, std::uint32_t value, std::uint32_t flags = 0) {
    const charlatan_event event{.sequence = 0, .produced_ns = 0, .value = value, .flags = flags};
    const auto written = ::write(fd, &event, sizeof(event));
    require(written == static_cast<ssize_t>(sizeof(event)), "event write failed");
}

charlatan_event read_event(int fd) {
    charlatan_event event{};
    const auto read_count = ::read(fd, &event, sizeof(event));
    require(read_count == static_cast<ssize_t>(sizeof(event)), "event read failed");
    return event;
}

charlatan_stats stats(int fd) {
    charlatan_stats result{};
    require(::ioctl(fd, CHARLATAN_IOC_GET_STATS, &result) == 0, "get stats failed");
    require(result.abi_version == CHARLATAN_ABI_VERSION, "unexpected ABI version");
    return result;
}

template <typename T>
T get_with_timeout(std::future<T>& future, const std::string& operation) {
    if (future.wait_for(1s) != std::future_status::ready) {
        std::cerr << "integration test timed out: " << operation << '\n';
        std::quick_exit(1);
    }
    return future.get();
}

void await_started(std::future<void>& future, const std::string& operation) {
    if (future.wait_for(1s) != std::future_status::ready) {
        std::cerr << "integration worker did not start: " << operation << '\n';
        std::quick_exit(1);
    }
    future.get();
}

void test_read_contracts() {
    auto fd = open_device(O_RDWR | O_NONBLOCK);
    reset_and_acknowledge(fd.get());
    charlatan_event event{};
    expect_errno(::read(fd.get(), &event, sizeof(event)), EAGAIN, "empty nonblocking read");
    expect_errno(::read(fd.get(), &event, 1), EINVAL, "short read buffer");

    write_event(fd.get(), 17, 3);
    void* read_only_page = ::mmap(nullptr, static_cast<std::size_t>(::getpagesize()), PROT_READ,
                                  MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    require(read_only_page != MAP_FAILED, "read-only test mapping failed");
    expect_errno(::read(fd.get(), read_only_page, sizeof(event)), EFAULT, "invalid read buffer");
    require(::munmap(read_only_page, static_cast<std::size_t>(::getpagesize())) == 0,
            "read-only test mapping cleanup failed");
    event = read_event(fd.get());
    require(event.value == 17 && event.flags == 3, "invalid-buffer read consumed an event");
}

void test_blocking_and_wraparound() {
    auto control = open_device(O_RDWR | O_NONBLOCK);
    reset_and_acknowledge(control.get());

    std::promise<void> started;
    auto started_future = started.get_future();
    auto reader = std::async(std::launch::async, [&started] {
        auto fd = open_device();
        started.set_value();
        return read_event(fd.get());
    });
    await_started(started_future, "blocking reader");
    std::this_thread::sleep_for(20ms);
    try {
        write_event(control.get(), 91);
    } catch (...) {
        (void)::ioctl(control.get(), CHARLATAN_IOC_RESET);
        try {
            (void)get_with_timeout(reader, "blocking reader cleanup");
        } catch (...) {
        }
        throw;
    }
    require(get_with_timeout(reader, "blocking read").value == 91, "blocking read did not receive event");

    reset_and_acknowledge(control.get());
    for (std::uint32_t value = 0; value < 128; ++value) {
        write_event(control.get(), value);
    }
    for (std::uint32_t value = 0; value < 64; ++value) {
        require(read_event(control.get()).value == value, "first ring segment lost ordering");
    }
    for (std::uint32_t value = 128; value < 192; ++value) {
        write_event(control.get(), value);
    }
    for (std::uint32_t value = 64; value < 192; ++value) {
        require(read_event(control.get()).value == value, "wrapped ring lost ordering");
    }
}

void test_poll_and_epoll() {
    auto fd = open_device(O_RDWR | O_NONBLOCK);
    reset_and_acknowledge(fd.get());
    pollfd readable_poll{.fd = fd.get(), .events = POLLIN, .revents = 0};
    require(::poll(&readable_poll, 1, 25) == 0, "empty device reported poll readability");
    write_event(fd.get(), 201);
    require(::poll(&readable_poll, 1, 200) == 1 && (readable_poll.revents & POLLIN), "poll missed queued event");
    require(read_event(fd.get()).value == 201, "poll event payload mismatch");
    readable_poll.revents = 0;
    require(::poll(&readable_poll, 1, 25) == 0, "drained device remained poll-readable");

    FileDescriptor epoll_fd(::epoll_create1(EPOLL_CLOEXEC));
    epoll_event registration{};
    registration.events = EPOLLIN | EPOLLPRI;
    registration.data.fd = fd.get();
    require(::epoll_ctl(epoll_fd.get(), EPOLL_CTL_ADD, fd.get(), &registration) == 0, "epoll add failed");
    epoll_event observed{};
    require(::epoll_wait(epoll_fd.get(), &observed, 1, 25) == 0, "empty device reported epoll readability");
    write_event(fd.get(), 202);
    require(::epoll_wait(epoll_fd.get(), &observed, 1, 200) == 1 && (observed.events & EPOLLIN),
            "epoll missed queued event");
    require(read_event(fd.get()).value == 202, "epoll event payload mismatch");

    auto control = open_device(O_RDWR | O_NONBLOCK);
    require(::ioctl(control.get(), CHARLATAN_IOC_RESET) == 0, "reset for priority readiness failed");
    pollfd priority_poll{.fd = fd.get(), .events = POLLPRI, .revents = 0};
    require(::poll(&priority_poll, 1, 200) == 1 && (priority_poll.revents & POLLPRI),
            "reset did not produce POLLPRI");
    require(::epoll_wait(epoll_fd.get(), &observed, 1, 200) == 1 && (observed.events & EPOLLPRI),
            "reset did not produce EPOLLPRI");
    charlatan_event reset_event{};
    expect_errno(::read(fd.get(), &reset_event, sizeof(reset_event)), ECANCELED, "priority reset acknowledgement");
    priority_poll.revents = 0;
    require(::poll(&priority_poll, 1, 0) == 0, "POLLPRI remained after reset acknowledgement");
}

void test_control_plane() {
    auto fd = open_device(O_RDWR | O_NONBLOCK);
    reset_and_acknowledge(fd.get());
    const auto before = stats(fd.get());
    std::uint32_t inject_count = before.queue_capacity + 1;
    require(::ioctl(fd.get(), CHARLATAN_IOC_INJECT_OVERFLOW, &inject_count) == 0, "overflow injection failed");
    const auto overflowed = stats(fd.get());
    require(overflowed.queue_depth == overflowed.queue_capacity, "overflow did not fill queue");
    require(overflowed.dropped == before.dropped + 1, "overflow policy did not drop newest event");
    pollfd writable{.fd = fd.get(), .events = POLLOUT, .revents = 0};
    require(::poll(&writable, 1, 0) == 0, "full queue reported write readiness");
    (void)read_event(fd.get());
    require(::poll(&writable, 1, 200) == 1 && (writable.revents & POLLOUT),
            "draining full queue did not wake write readiness");
    for (std::uint32_t value = 1; value < overflowed.queue_capacity; ++value) {
        (void)read_event(fd.get());
    }

    charlatan_fault_config faults{.abi_version = CHARLATAN_ABI_VERSION, .fail_next_reads = 1,
                                   .fail_next_writes = 1, .producer_control_delay_ms = 0};
    require(::ioctl(fd.get(), CHARLATAN_IOC_SET_FAULTS, &faults) == 0, "fault setup failed");
    charlatan_event event{};
    expect_errno(::write(fd.get(), &event, sizeof(event)), EIO, "injected write fault");
    write_event(fd.get(), 303);
    expect_errno(::read(fd.get(), &event, sizeof(event)), EIO, "injected read fault");
    require(read_event(fd.get()).value == 303, "read fault consumed event");

    charlatan_rate_config bad_rate{.abi_version = 0, .period_ms = 1};
    expect_errno(::ioctl(fd.get(), CHARLATAN_IOC_SET_RATE, &bad_rate), EINVAL, "bad rate ABI");
    charlatan_rate_config rate{.abi_version = CHARLATAN_ABI_VERSION, .period_ms = 5};
    require(::ioctl(fd.get(), CHARLATAN_IOC_SET_RATE, &rate) == 0, "start producer failed");
    pollfd producer_poll{.fd = fd.get(), .events = POLLIN, .revents = 0};
    require(::poll(&producer_poll, 1, 500) == 1, "producer did not generate event");
    (void)read_event(fd.get());
    require(::ioctl(fd.get(), CHARLATAN_IOC_PAUSE) == 0, "pause producer failed");

    expect_errno(::ioctl(fd.get(), _IO('x', 1)), ENOTTY, "invalid ioctl");
}

void test_concurrent_producer_control() {
    auto control = open_device(O_RDWR | O_NONBLOCK);
    auto pauser = open_device(O_RDWR | O_NONBLOCK);
    auto resumer = open_device(O_RDWR | O_NONBLOCK);
    charlatan_rate_config initial_rate{.abi_version = CHARLATAN_ABI_VERSION, .period_ms = 2};
    require(::ioctl(control.get(), CHARLATAN_IOC_SET_RATE, &initial_rate) == 0, "initial rate setup failed");

    charlatan_fault_config fault{.abi_version = CHARLATAN_ABI_VERSION, .fail_next_reads = 0,
                                  .fail_next_writes = 0, .producer_control_delay_ms = 100};
    require(::ioctl(control.get(), CHARLATAN_IOC_SET_FAULTS, &fault) == 0, "control delay setup failed");
    auto pause_worker = std::async(std::launch::async, [&] {
        return ::ioctl(pauser.get(), CHARLATAN_IOC_PAUSE);
    });
    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    auto snapshot = stats(control.get());
    while (!(snapshot.producer_running == 0 && snapshot.producer_control_delay_active == 1 &&
             snapshot.producer_control_waiters == 1) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
        snapshot = stats(control.get());
    }
    require(snapshot.producer_running == 0 && snapshot.producer_control_delay_active == 1 &&
                snapshot.producer_control_waiters == 1,
            "pause did not enter its injected delay");
    auto resume_worker = std::async(std::launch::async, [&] { return ::ioctl(resumer.get(), CHARLATAN_IOC_RESUME); });
    snapshot = stats(control.get());
    while (!(snapshot.producer_running == 0 && snapshot.producer_control_delay_active == 1 &&
             snapshot.producer_control_waiters == 2) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
        snapshot = stats(control.get());
    }
    require(snapshot.producer_running == 0 && snapshot.producer_control_delay_active == 1 &&
                snapshot.producer_control_waiters == 2,
            "resume did not enter the control path during pause");
    require(resume_worker.wait_for(0ms) == std::future_status::timeout,
            "resume completed before pause released producer control");
    reset_and_acknowledge(control.get());
    require(get_with_timeout(pause_worker, "delayed pause") == 0, "delayed pause failed");
    require(get_with_timeout(resume_worker, "delayed resume") == 0, "delayed resume failed");
    reset_and_acknowledge(control.get());
    const auto final_snapshot = stats(control.get());
    require(final_snapshot.producer_running == 1, "resume did not leave producer enabled");
    pollfd producer_poll{.fd = control.get(), .events = POLLIN, .revents = 0};
    require(::poll(&producer_poll, 1, 500) == 1 && (producer_poll.revents & POLLIN),
            "producer did not restart after concurrent control");
    (void)read_event(control.get());
    fault.producer_control_delay_ms = 0;
    require(::ioctl(control.get(), CHARLATAN_IOC_SET_FAULTS, &fault) == 0, "control delay cleanup failed");
    require(::ioctl(control.get(), CHARLATAN_IOC_PAUSE) == 0, "final producer pause failed");
}

void test_reset_wakes_blocked_reader() {
    auto control = open_device(O_RDWR | O_NONBLOCK);
    reset_and_acknowledge(control.get());
    std::promise<void> started;
    auto started_future = started.get_future();
    auto reader = std::async(std::launch::async, [&started] {
        auto fd = open_device();
        started.set_value();
        charlatan_event event{};
        const auto result = ::read(fd.get(), &event, sizeof(event));
        return std::pair{result, errno};
    });
    await_started(started_future, "reset-canceled reader");
    std::this_thread::sleep_for(20ms);
    try {
        require(::ioctl(control.get(), CHARLATAN_IOC_RESET) == 0, "reset while blocked failed");
    } catch (...) {
        (void)::ioctl(control.get(), CHARLATAN_IOC_RESET);
        (void)get_with_timeout(reader, "reset reader cleanup");
        throw;
    }
    const auto [result, error] = get_with_timeout(reader, "reset-canceled read");
    require(result == -1 && error == ECANCELED, "reset did not cancel blocked reader");
}

}  // namespace

int main() {
    try {
        test_read_contracts();
        test_blocking_and_wraparound();
        test_poll_and_epoll();
        test_control_plane();
        test_concurrent_producer_control();
        test_reset_wakes_blocked_reader();
        std::cout << "integration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "integration test failed: " << error.what() << '\n';
        return 1;
    }
}
