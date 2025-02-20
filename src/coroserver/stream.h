#include "coroutines.h"

namespace coroserver{

class IStream {
public:
    virtual ~IStream() = default; 
    ///receive data asynchronously
    /**
     * @return string_view contains received data. It is always returned at least
     * one byte length data. If returned empty buffer, timeout or eof has been reached.
     * Use is_eof() to determine what happened
     * 
     * @note the function is not concurrency safe. Only one coroutine can await on this method.
     * It is still posible to write during awaiting
     */
    virtual awaitable<std::string_view> receive() = 0;
    ///put back some data to be received later
    /**
     * @param s a view contains data to put back. This should be part of data returned by
     * last receive(). You can put back a view to different view, but you must ensure that
     * underlying buffer remain valid until the data are retrieved. You can put_back only
     * one view, previous put view is replaced
     */ 
    virtual void put_back(std::string_view s) = 0;
    ///returns true, if eof has been reached by last read
    /** 
     * @retval true last read failed because eof
     * @retval false last read didn't failed or failed because timeout
     */
    virtual bool is_eof() const = 0;
    /// send buffer
    /** 
     * @param data to send
     * @retval true data successfuly left output buffer to networ
     * @retval false data has been discarded, because network error (this also closes the connection)
     * @note you can discard awaitable object. The function should always send the buffer, but by 
     * discarding awaitable also discard status of the connection. 
     * @note the function can immediately return false if connection is already closed
     * 
     * @note the function is concurrency safe. Multiple coroutines can await
     */

    virtual awaitable<bool> send(std::string_view data) = 0;
    
    /// Sends eof and closes outgoing connection
    virtual void send_eof() = 0;
};

class Stream {
public:

    Stream(std::shared_ptr<IStream> ptr):_ptr(std::move(ptr)) {}
    awaitable<std::string_view> receive() {
        return _ptr->receive();
    }
    void put_back(std::string_view s){
        _ptr->put_back(s);
    }
    bool is_eof() const{
        return _ptr->is_eof();
    }
    awaitable<bool> send(std::string_view data) {
        return _ptr->send(data);
    }
    void send_eof() {
        _ptr->send_eof();
    }

    


protected:
    std::shared_ptr<IStream> _ptr;
};

}