#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>

namespace {

class FileDescriptor {
public:
    explicit FileDescriptor(const std::string& path) : value_(::open(path.c_str(), O_RDONLY | O_CLOEXEC)) {
        if (value_ < 0) {
            throw std::system_error(errno, std::generic_category(), "open " + path);
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    ~FileDescriptor() {
        if (value_ >= 0) {
            (void)::close(value_);
        }
    }

private:
    int value_;
};

}  // namespace

int main(int argc, char* argv[]) {
    const std::string device = argc == 2 ? argv[1] : "/dev/charlatan0";

    if (device == "--help") {
        std::cout << "usage: charlatan-smoke [device-path]\n";
        return 0;
    }

    if (argc > 2) {
        std::cerr << "usage: charlatan-smoke [device-path]\n";
        return 2;
    }

    try {
        const FileDescriptor fd(device);
        std::cout << "opened " << device << '\n';
        return 0;
    } catch (const std::system_error& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
