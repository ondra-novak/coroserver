#pragma once

#include "stream.h"

namespace coroserver {

template<typename T>
class BufferedStreamT: public T {
public:
    using ABool = awaitable<bool>::result;


    using T::T;
    virtual awaitable<bool> send(std::string_view data) {
        prepared_coro p; //allows to execute callback outside of lock
        std::lock_guard _(_mx);     //lock
        if (_close_req || _closed) return false;     //if closed - return false
        if (!_buffer_flushing.empty()) { //if flushing buffer is not empty
            //insert data to pending buffer
            _buffer_pending.insert(_buffer_pending.end(), data.begin(), data.end());
            //return lambda that allows to set awaiter on completion
            return [this](ABool prom) -> prepared_coro {
                if (prom) {
                    std::lock_guard _(_mx);
                    //if all buffers are empty,
                    if (_buffer_flushing.empty() && _buffer_pending.empty()) {
                        //we can resolve promise now
                        return prom(true);
                    }
                    //push to pending awaiters
                    _awaiters_pending.push_back(std::move(prom));
                }
                //no coroutine
                return {};
            };
        } else {
            //put data to buffer flushing and flush it now
            _buffer_flushing.insert(_buffer_flushing.end(), data.begin(), data.end());
            //flush - but not yet - keep prepared_coro
            p = _callback.await(T::send(
                    std::string_view(_buffer_flushing.data(), _buffer_flushing.size())),
                    this);
        }
        return [this, p = std::move(p)](ABool r) mutable ->prepared_coro {
            if (r) {
                std::lock_guard _(_mx);
                //store promise
                _awaiters_flushing.push_back(std::move(r));
            }
            //check if context is available
            auto ctx = T::get_async_context();
            //run flush outside of context
            if (ctx) ctx->enqueue(std::move(p));
            //otherwise it will run now
            return std::move(p);
        };
    }
    virtual awaitable<bool> close() {
        std::lock_guard _(_mx);
        //if buffers are empty, we can close now
        if (_buffer_flushing.empty() && _buffer_pending.empty()) {
            //mark closed
            _closed = true;
            //close
            return T::close();
        } else{
            //mark that we want to send eof
            _close_req = true;
            //this must be done asynchronously
            return [this](ABool prom) ->prepared_coro{
                std::lock_guard _(_mx);
                if (_closed) {
                    return prom(true);
                } else {
                    _close_completion = std::move(prom);
                    return {};
                }
            };
        }
    }

protected:


    ///mutex
    std::mutex _mx;
    ///list of awaiters in pending buffer
    std::vector<ABool> _awaiters_pending;
    ///list of awaiters in flushing buffer
    std::vector<ABool> _awaiters_flushing;
    ///pending buffer - ready to send,
    std::vector<char> _buffer_pending;
    ///flushing buffer - data in transit
    std::vector<char> _buffer_flushing;
    ///awaiter waiting for close completion
    ABool _close_completion;
    ///close stream when done
    bool _close_req = false;
    ///callback is used for T::close
    bool _close_compl = false;
    ///stream closed
    bool _closed = false;

    prepared_coro send_complete(awaitable<bool> &awt) {
        while (true) {
            //in all cases, clear flushing buffer
            _buffer_flushing.clear();
            try {
                //retrieve state
                bool st = awt;
                //if this was close completion
                if (_close_compl) {
                    std::lock_guard _(_mx);
                    //set closed
                    _closed = true;
                    //finish closed completion
                    return _close_completion(st);
                }
                //process all flushing awaiters
                for (auto &x: _awaiters_flushing) x(st);
                //clear this list
                _awaiters_flushing.clear();
                bool close_req;
                {
                    //under lock
                    std::lock_guard _(_mx);
                    //swap awaiters
                    std::swap(_awaiters_flushing, _awaiters_pending);
                    //swap buffers
                    std::swap(_buffer_flushing, _buffer_pending);
                    //read close req
                    close_req = _close_req;
                    //store closed state
                    _closed = !st;
                    //if write status is ok
                    if (st) {
                        //if buffers are not empty
                        if (!_awaiters_flushing.empty() || !_buffer_flushing.empty()) {
                            //continue in sending T::send over callback
                            //this function will be called again
                            return _callback.await_cont(T::send(
                                    std::string_view(_buffer_flushing.data(), _buffer_flushing.size())));
                        //if close requested
                        } else if (close_req) {
                            //we will await in T::close()
                            _close_compl = true;
                            //await on T::close()
                            return _callback.await_cont(T::close());
                        }
                    } else {
                        //if there are data
                        if (!_awaiters_flushing.empty() || !_buffer_flushing.empty()) {
                            //call this function again
                            continue;
                        }
                    }
                }
                return {};
            } catch (...) {
                auto e = std::current_exception();
                if (_close_compl) {
                    std::lock_guard _(_mx);
                    return _close_completion.set_exception(e);
                }
                std::vector<ABool> tmpa;
                std::vector<ABool> tmpb;
                ABool c;
                {
                    std::lock_guard _(_mx);
                    tmpa = std::move(_awaiters_flushing);
                    tmpb = std::move(_awaiters_pending);
                    c = std::move(_close_completion);
                    _buffer_flushing.clear();
                    _buffer_pending.clear();
                    _closed =true;
                }
                for (auto &x: tmpa) x.set_exception(e);
                for (auto &x: tmpb) x.set_exception(e);
                c.set_exception(e);
                return {};
            }
        }

    }




    await_member_callback<bool,BufferedStreamT *,
        &BufferedStreamT::send_complete> _callback;


};

using BufferedStream = BufferedStreamT<StreamProxy>;


}

