#pragma once
#include "context.hpp"
#include "stream.hpp"
#include <span>
#include <map>
#include <string>
#include <string_view>


namespace coroserver {



///holds environment. Can be used to set or delete environment variables
/**
 * @note Empty environment is not possible. When empty environment is passed to the
 * spawn_process, it defaults to current enironment
 *
 * To retrieve current enviromnent, call current()
 */
class Environment: public std::map<std::string, std::string> {
public:

    ///Creates empty environment.
    /**
     * If empty environment is passed to the spawn_process, current environment is used.
     * If you really need to empty whole environment, you need to insert at least one field
     * You can insert field with empty key, which is not exported
     */
    Environment() = default;

    using std::map<std::string, std::string>::map;

    ///Loads current enviromnent
    static Environment current();



    ///merge fields from one environment into current enviromnent
    void merge(const Environment &other) {
        for (const auto &[k, v]: other) {
            (*this)[k] = v;
        }
    }
};



///Spawns a new process and creates stream with that process
/**
 * @param ctx context
 * @param path path to process
 * @param args arguments starting by 1st argument (do not pass path as arg[0],
 *          this is handled automatically)
 * @param env environment variables, empty = use default
 * @return Connected stream
 *
 */
Stream spawn_process(Context ctx, std::string_view path, std::span<const std::string_view> args, const Environment &env = {});

///Spawns a new process and creates stream with that process
inline Stream spawn_process(Context ctx, std::string_view path, std::initializer_list<std::string_view> args, const Environment &env = {} )  {
    return spawn_process(std::move(ctx), path, std::span<const std::string_view>(args.begin(), args.end()), env);
}
///Terminate process associated withe stream
/**
 * @param stream stream created by function spawn_process.
 * @retval true process termination requested
 * @retval false operation cannot be performed (no valid stream, process exited)
 */
bool terminate_process(Stream stream);

///Retrieve exit code of process associated with the stream
/**
 * @param stream a stream create by function spawn_process
 * @param tp specifies timeout for waiting for process exit. Default value causes infinite waiting
 * @return awaitable. You need co_await. Result is exit code of the process, which is direct
 * value or return from main. The actuall exit code depends on platform. In linux, negative value
 * means, that process has been terminated by a signal.
 *
 * @note you don't need to retrieve exit status. If stream is closed, the exit code is
 * discarded (but no zombie is left)
 */
coro::awaitable<int> get_process_exit_status(Stream stream, std::chrono::system_clock::time_point tp = std::chrono::system_clock::time_point::max());


///Retrieve exit code of process associated with the stream
/**
 * @param stream a stream create by function spawn_process
 * @param dur duration waiting
 * @return awaitable
 *
 * @see get_process_exit_status
 */
template<typename A, typename B>
coro::awaitable<int> get_process_exit_status(Stream stream, std::chrono::duration<A,B> dur) {
    return get_process_exit_status(std::move(stream),std::chrono::system_clock::now()+dur);
}



}
