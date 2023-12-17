/*
 * pipe.h
 *
 *  Created on: 7. 5. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_PIPE_H_
#define SRC_COROSERVER_PIPE_H_


#include "async_support.h"
#include "defs.h"
#include "stream.h"
#include <coro.h>

namespace coroserver {

class ContextIOImpl;

class PipeStream: public AbstractStreamWithMetadata {
public:
    PipeStream(AsyncSupport context, int fdread, int fdwrite,TimeoutSettings tms);
    ~PipeStream();
    virtual coro::future<std::string_view> read() override;
    virtual std::string_view read_nb() override;
    virtual bool is_read_timeout() const override;
    virtual coro::future<bool> write(std::string_view buffer) override;
    virtual coro::future<bool> write_eof() override;
    virtual coro::suspend_point<void> shutdown() override;
    virtual Counters get_counters() const noexcept override;
    virtual PeerName get_peer_name() const override;

    ///Create pipe
    /**
     * @return it is created one object which can be read and written and which
     * works as echo device, so anything written into it can be read from. However
     * the connection is made through the kernel's pipe object, which allows to
     * safely separate both ends in different threads.
     *
     */
    static Stream create(AsyncSupport context, TimeoutSettings tms = {});

    ///duplicate read descriptor (to be used with POSIX interface)
    /**
     * @param stream stream create by the create() function
     * @param share set true, if you need to share the descriptor into child process
     * @param close set true to close original descriptor inside of the object
     * @param dup_to target descriptor number, default value is used to create new descriptor
     * @return duplicated descriptor
     */
    static int dupRead(Stream stream, bool share, bool close, int dup_to = -1);
    ///duplicate write descriptor (to be used with POSIX interface)
    /**
     * @param stream stream create by the create() function
     * @param share set true, if you need to share the descriptor into child process
     * @param close set true to close original descriptor inside of the object
     * @param dup_to target descriptor number, default value is used to create new descriptor
     * @return duplicated descriptor
     */
    static int dupWrite(Stream stream, bool share, bool close, int dup_to = -1);

    ///Create stream from file descriptor
    /**
     * @param fd file descriptor
     * @return stream
     *
     * @note The descriptor is duplicated. you need to create own descriptor if you
     * no longer needed.
     */
    static Stream create(AsyncSupport context, int fd, TimeoutSettings tms = {});

    ///Create stream from file descriptor
    /**
     * @param fdread read descriptor
     * @param fdwrite write descriptor
     * @return stream
     *
     * @note The descriptor is duplicated. you need to create own descriptor if you
     * no longer needed.
     */
    static Stream create(AsyncSupport context, int fdread, int fdwrite, TimeoutSettings tms = {});

    ///Create stream which is connected to stdin and stdout
    /**
     * @param context io context
     * @param tms timeout settings
     * @param duplicate set true to duplicate descriptors, false to use standard descriptors
     * @return stream
     */
    static Stream stdio(AsyncSupport context, TimeoutSettings tms = {}, bool duplicate = true);

protected:
    AsyncSupport _ctx;
    int _fdread;
    int _fdwrite;
    Counters _cntr;
    coro::generator<std::string_view> _reader;
    coro::generator<bool, std::string_view> _writer; //writer

    std::vector<char> _read_buffer;
    bool _is_timeout = false;
    bool _is_eof = false;
    bool _is_closed = false;
    std::size_t _last_read_full = 0;
    std::size_t _new_buffer_size = 1024;



    coro::generator<std::string_view> start_read();
    coro::generator<bool, std::string_view> start_write();
};


}


#endif /* SRC_COROSERVER_PIPE_H_ */
