#include "event_fd.h"

#include <sys/eventfd.h>
#include <sys/poll.h>
#include <system_error>
#include <tuple>
#include <unistd.h>
#include <utility>

namespace coroserver {

FileDescriptor::FileDescriptor(FileDescriptor &&other):_fd(std::exchange(other._fd, -1)) {}

FileDescriptor::~FileDescriptor() {
    if (_fd >= 0) ::close(_fd);
}

EventFD::EventFD(Mode mode):EventFD(mode, 0) {
}

EventFD::EventFD(Mode mode, unsigned int initial_value)
    :_fd(eventfd(initial_value, (mode == semaphore?EFD_SEMAPHORE:0) | EFD_CLOEXEC | EFD_NONBLOCK)) {
}


void EventFD::wait() {
    pollfd pfd = {_fd, POLLIN, 0};
    if (poll(&pfd, 1, -1)<0) throw std::system_error(errno, std::system_category());
}

bool EventFD::wait_until(const std::chrono::system_clock::time_point &tp) {
    auto now = std::chrono::system_clock::now();
    int timeout = 0;
    if (now  < tp) timeout = std::chrono::duration_cast<std::chrono::milliseconds>(tp - now).count();
    pollfd pfd = {_fd, POLLIN, 0};
    int r = poll(&pfd, 1, timeout);
    if (r<0) throw std::system_error(errno, std::system_category());
    return r>0;
}

void EventFD::add(unsigned int val) {
    eventfd_t v = val;
    int r = eventfd_write(_fd, v);
    if (r < 0) {
        int err = errno;
        throw std::system_error(err, std::system_category());
    }
}

unsigned int EventFD::fetch_and_reset() {
    eventfd_t val = 0;
    int r = eventfd_read(_fd, &val);
    if (r < 0) {
        int err = errno;
        if (err == EAGAIN) return val;
        throw std::system_error(err, std::system_category());
    }
    return static_cast<unsigned int>(val);
}

}

