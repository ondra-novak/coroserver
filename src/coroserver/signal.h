/*
 * signal.h
 *
 *  Created on: 11. 1. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_SIGNAL_H_
#define SRC_COROSERVER_SIGNAL_H_



#include "stream.h"
#include "io_context.h"

#include <cocls/future.h>
#include <vector>

namespace coroserver {

class SignalHandler {
public:
    SignalHandler(ContextIO ioctx);
    ~SignalHandler();

    cocls::future<void> operator()(int signal);
    cocls::future<void> operator()(std::initializer_list<int > signals);


protected:
    Stream _signal_stream;
    using Promises = std::vector<std::shared_ptr<cocls::promise<void> >  >;
    std::unordered_map<int, Promises> _listeners;
    std::mutex _mx;

    cocls::suspend_point<void> on_signal(cocls::future<std::string_view> &f) noexcept;

    cocls::call_fn_future_awaiter<&SignalHandler::on_signal> _awt;


};


cocls::future<void> wait_for_signal(std::initializer_list<int> list_of_signals);
cocls::future<void> wait_for_signal(int list_of_signals);


}



#endif /* SRC_COROSERVER_SIGNAL_H_ */
