/*
 * character_io.h
 *
 *  Created on: 8. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_CHARACTER_IO_H_
#define SRC_COROSERVER_CHARACTER_IO_H_

#include <coro.h>
#include "stream.h"
#include <array>

namespace coroserver {


///Character reader allows to read characters from the stream in coroutine
/**
 Character reader is awaitable object. To read characters, you need to co_await on
 the instance of the object. It is also possible to read synchronously, in this case,
 the object works as a function, which returns next character.

 In both cases, it returns EOF, if end of stream has been reached

 In coroutine mode, object is optimized to skip any suspension when next character
 is ready to be returned.

 In perspective of access to the stream, existence of this object attached to
 a stream is considered as an access. You should avoid multiple accesses to the stream
 otherwise UB. If you need to read stream using standard API, you need to destroy this
 object first.
 */
template<typename Stream>
class CharacterReader {
public:
    ///construct reader
    CharacterReader(Stream s):_s(std::move(s)) {}
    ///object cannot be copies
    CharacterReader(const CharacterReader &other)= delete;
    ///object ca be moved
    CharacterReader(CharacterReader &&other)
        :_s(std::move(other._s))
        ,_ptr(other._ptr)
        ,_end(other._end) {
        other._ptr = other._end;
    }
    ///object cannot be assigned
    CharacterReader &operator=(const CharacterReader &other)= delete;
    ///object cannot be assigned
    CharacterReader &operator=(CharacterReader &&other)= delete;
    ///determines, whether next character is ready
    bool await_ready() const {
        return _ptr != _end;
    }
    ///suspend when reading next character
    bool await_suspend(std::coroutine_handle<> h) {

        coro::target_coroutine(_target, h, nullptr);
        //perform reading into future
        _fut << [&]{return _s.read();};

        return _fut.register_target_async(_target);
    }
    ///called after resume to retrieve a value
    int await_resume() {
        //if no character ready - we need to fetch from foture
        if (_ptr == _end) {
            //fetch from future
            const std::string_view &text = _fut;
            //if text is empty, this is eof
            if (text.empty()) return -1;
            //update pointers
            _ptr = text.data();
            _end = _ptr+text.size();
        }
        //return next characters
        return static_cast<unsigned char>(*(_ptr++));
    }
    ///destructor - returns back unprocessed input
    ~CharacterReader() {
        //return back unprocessed input
        _s.put_back(std::string_view(_ptr, _end-_ptr));
    }

    ///synchronous access, returns next character
    /**
     * @return next character or -1 if eof
     */
    int operator()() {
        if (_ptr == _end) {
            _fut << [&]{return _s.read();};
            _fut.wait();
            std::string_view &text = _fut.value();
            if (text.empty()) return -1;
            _ptr = text.data();
            _end = _ptr+text.size();
        }
        return static_cast<unsigned char>(*(_ptr++));
    }
    void put_back() {
        --_ptr;
    }

protected:
    Stream _s;
    coro::future<std::string_view>::target_type _target;
    coro::future<std::string_view> _fut;
    const char *_ptr = nullptr;
    const char *_end = nullptr;
};

template<typename Stream>
CharacterReader(Stream) -> CharacterReader<Stream>;


///Character writer allows to write character to the stream which automatic caching
/**
 * @tparam buffer_size specifies size of internal buffer. Written data are stored into
 * buffer until the buffer is full, then the buffer is asynchronously flushed.
 *
 * @note You need to explicitly flush buffer before it is destroyed, otherwise remaining
 * data will be lost. This rule is enforced because flush is awaitable and need to
 * be called with co_await
 *
 * To use this class, use it as function which accepts the character. The result of this
 * function must be awaited by co_await. Discarding the return value can cause synchronous
 * flush.The object is  movable and copyable including the data.
 */

#ifdef COROSERVER_DEFAULT_WRITE_BUFFER_SIZE
template<typename Stream, std::size_t buffer_size=(COROSERVER_DEFAULT_WRITE_BUFFER_SIZE)>
#else
template<typename Stream, std::size_t buffer_size=8192>
#endif
class CharacterWriter {
public:
    ///construct the object
    CharacterWriter(Stream s):_s(s) {}

    ///awaiter which is returned from write, allows to fast co_await in case of asynchronous access
    class Awaitable {
    public:
        Awaitable(CharacterWriter &owner, bool flush):_owner(owner),_state(flush?flush_req:noflush) {}
        Awaitable(const Awaitable &) = delete;
        Awaitable &operator=(const Awaitable &) = delete;
        bool await_ready() const {
            return _state != flush_req;
        }
        bool await_suspend(std::coroutine_handle<> h) {
            coro::target_coroutine(_target, h);
            _fut << [&]{return _owner._s.write(std::string_view(_owner._buffer.data(), _owner._pos));};
            _owner._pos = 0;
            return _fut.register_target_async(_target);
        }
        bool await_resume() {
            switch (_state) {
                case flushed_ok:
                case noflush: return true;
                case flush_req: {
                    bool b = _fut;
                    _state = b?flushed_ok:flushed_fail;
                    return b;
                }
                default:
                case flushed_fail:return false;
            };
        }

        ///by readying boolean value as result synchronous flush is requested
        operator bool() {
            switch (_state) {
                case flushed_ok:
                case noflush: return true;
                case flush_req: {
                    bool b = _owner._s.write(std::string_view(_owner._buffer, _owner._pos)).wait();
                    _owner._pos = 0;
                    _state = b?flushed_ok:flushed_fail;
                    return b;
                }
                default:
                case flushed_fail:return false;
            };
        }

        ~Awaitable() {
            if (_state == flush_req) {
                _owner._s.write(std::string_view(_owner._buffer.data(), _owner._pos)).wait();
                _owner._pos = 0;
            }
        }

    protected:
        CharacterWriter &_owner;
        enum State {
            noflush,
            flush_req,
            flushed_ok,
            flushed_fail
        };
        State _state;
        coro::future<bool> _fut;
        coro::future<bool>::target_type _target;
    };

    ///write the character
    /**
     * @param c character to write
     * @return awaitable object, you need to co_await on it. Return value of co_await
     * is true if write was successful, or false, if not
     */
    Awaitable operator()(char c) {
        _buffer[_pos] = c;
        ++_pos;
        return Awaitable(*this, _pos >= buffer_size);
    }

    ///flush all data to the output stream
    /**
    * @return awaitable object, you need to co_await on it. Return value of co_await
    * is true if write was successful, or false, if not
    */
    Awaitable flush() {
        return Awaitable(*this, _pos > 0);
    }


    ~CharacterWriter() {
    }



protected:
    Stream _s;
    std::array<char, buffer_size>  _buffer;
    std::size_t _pos;
};

template<typename Stream>
CharacterWriter(Stream) -> CharacterWriter<Stream>;


}


#endif /* SRC_COROSERVER_CHARACTER_IO_H_ */
