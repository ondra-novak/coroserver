/*
 * idispatcher.h
 *
 *  Created on: 27. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_IPOLLER_H_
#define SRC_USERVER_IPOLLER_H_

#include "async_support.h"

#include <coro.h>



namespace coroserver {




///Interface defines abstract dispatcher
/**
 * Dispatcher is single thread class, which monitors collection of asynchronous resources. You can
 * also define own type of asynchronous resource.
 *
 * The main goal is to monitor the collection, and when the particular resource is signaled, it
 * should return a task - structure which contains callback function and result of the monitoring.
 *
 * The task is later called.
 *
 * The monitoring is blocking, so during the operation, the thread is blocked
 *
 * @see getTask
 *
 */


class IPoller: public IAsyncSupport {
public:

    ///starts poller
    /**
     * @param scheduler reference to scheduler, which is used to schedule
     * requests
     * @return future which is evaluated when worker finishes. It finishes by
     * calling stop()
     */
    virtual coro::future<void> start(coro::scheduler &scheduler) = 0;

    ///stops poller
    /**
     * Causes canceling all pending requests and stop internal worker
     */
    virtual void stop() = 0;




	virtual ~IPoller() {}
};




}



#endif /* SRC_USERVER_IPOLLER_H_ */
