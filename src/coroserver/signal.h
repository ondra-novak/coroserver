/*
 * signal.h
 *
 *  Created on: 11. 1. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_SIGNAL_H_
#define SRC_COROSERVER_SIGNAL_H_



#include "stream.h"
#include "async_support.h"

#include <cocls/future.h>

#include <unordered_map>
#include <vector>

namespace coroserver {

///Creates asynchronous signal handler
/**This object is attached to global signal handler provided by
 * coroserver library. You can use it to co_await on signal through
 * the operator(). You can co_await on multiple signals at once
 *
 * The implementation install the signal handler to the awaited signals
 * only (other signals are not touched).
 *
 */
class SignalHandler {
public:
    ///Constructs the object
    /**
     * @param ioctx io context or asynchronou support object
     */
    SignalHandler(AsyncSupport ioctx);
    ///Destroy object
    /**
     * Destruction of object causes that awaiting is canceled. Awaiting
     * coroutines receives exception await_canceled_exception
     */
    ~SignalHandler();

    ///Await on signal
    /**
     * @param signal signal to await
     * @return future which can be co_awaited
     * @exception cocls::await_canceled_exception - object has been destroyed
     * without catching a signal
     */
    cocls::future<void> operator()(int signal);
    ///Await on many signals
    /**
     * @param signals signals to await
     * @return future which can be co_awaited
     * @exception cocls::await_canceled_exception - object has been destroyed
     * without catching a signal
     */
    cocls::future<void> operator()(std::initializer_list<int > signals);


protected:
    Stream _signal_stream;
    using Promises = std::vector<std::shared_ptr<cocls::promise<void> >  >;
    std::unordered_map<int, Promises> _listeners;
    std::mutex _mx;

    cocls::suspend_point<void> on_signal(cocls::future<std::string_view> &f) noexcept;

    cocls::call_fn_future_awaiter<&SignalHandler::on_signal> _awt;


};



}



#endif /* SRC_COROSERVER_SIGNAL_H_ */
