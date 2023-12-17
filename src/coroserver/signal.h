/*
 * signal.h
 *
 *  Created on: 11. 1. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_SIGNAL_H_
#define SRC_COROSERVER_SIGNAL_H_




#include <coro.h>

namespace coroserver {

using HandlerID = const void *;


coro::future<unsigned int> signal(int signum, HandlerID id = {});

coro::future<unsigned int> signal(std::initializer_list<int> signums, HandlerID id = {});

coro::future<unsigned int>::pending_notify cancel_signal_await(HandlerID id);


}



#endif /* SRC_COROSERVER_SIGNAL_H_ */
