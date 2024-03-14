#pragma once

#include <chrono>
#include <cstdint>

namespace coroserver {

class FileDescriptor {
public:

    FileDescriptor(int fd):_fd(fd) {}
    FileDescriptor(FileDescriptor &&other);
    ~FileDescriptor();
    operator int () const {return _fd;}

protected:
    int _fd =-1;
};

///wraps eventfd into C++ object
class EventFD {
public:

    ///eventfd mode
    enum Mode {
        ///works as semaphore - see EFDSemaphore
        semaphore,
        ///works as event accumulator - see EFDEventRegister
        event_register
    };

    ///Initialize
    /**
     * @param mode mode of operation
     */
    EventFD(Mode mode);
    ///Initialize the object with an initial value
    /**
     * @param mode mode of operation
     * @param initial_value initial value
     */
    EventFD(Mode mode, unsigned int initial_value);
    //copy and assigmnet are deleted by default

    ///block while internal counter is equal to zero.
    /**
     * Wakes up, when internal counter is set to one
     *
     * @note MT-Safe
     */
    void wait();

    ///block until internal counter is greater then zero or timeout whatever happens sooner
    /**
     * @param tp timepoint until wait
     * @retval true counter is greater than zero
     * @retval false counter is zero and timeout happened
     */
    bool wait_until(const std::chrono::system_clock::time_point &tp);

    ///block for given duration
    /**
     * @param dur duration
     * @retval true counter is greater than zero
     * @retval false counter is zero and timeout happened
     */
    template<typename A, typename B>
    bool wait_for(std::chrono::duration<A,B> dur) {
        return wait_until(std::chrono::system_clock::now() + dur);
    }

    ///Retrieve internal file descriptor, can be used with poll or zmq_poll
    int getFD() const {return _fd;}

    void add(unsigned int val);

    unsigned int fetch_and_reset();

public:

    FileDescriptor _fd;
};


///EventFD works as Semaphore
/**
 * Semaphore can be locked when internal counter is above zero, Each lock
 * decreases counter. Once counter is 0, the semaphore can't be locked anymor,
 * You need to unlock semaphore to increase the counter
 */
class EFDSemaphore: public EventFD {
public:
    ///Construct semaphore
    /**
     * @param initial_value initial count of locks
     */
    EFDSemaphore(unsigned int initial_value): EventFD(semaphore, initial_value) {}

    ///lock the semaphore (blocking)
    void lock() {
        while (!try_lock()) {
            wait();
        }
    }

    ///unlock the semaphore
    void unlock() {
        add(1);
    }

    ///try to lock
    /**
     *
     * @retval true locked
     * @retval false counter is zero
     */
    bool try_lock() {
        return fetch_and_reset() != 0;
    }

};

///Event register - registers events, atomically resets counter when attempt to read it
class EFDEventRegister: public EventFD {
public:
    ///Initialize
    /**
     * @param initial_value optional initial value
     */
    EFDEventRegister(unsigned int initial_value = 0): EventFD(event_register, initial_value) {}

    ///register event
    void regEvent() {
        add(1);
    }

    ///register more events
    void regEvents(unsigned int count) {
        add(count);
    }

    ///fetch count of events and reset (atomically)
    using EventFD::fetch_and_reset;

    ///fetch count, if there is no event, wait (blocking), until and event is registered
    /**
     * @return count of events registered
     *
     * @note internal counter is reset atomically
     */
    unsigned int wait_and_fetch() {
        unsigned int r = fetch_and_reset();
        while (r == 0) {
            wait();
            r = fetch_and_reset();
        }
        return r;
    }

};

}



