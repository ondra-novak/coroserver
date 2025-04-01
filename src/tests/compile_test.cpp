#include "../coroserver/stream.hpp"
#include "../coroserver/buffered_stream.hpp"
#include "../coroserver/handle_hash_map.hpp"
#include "../coroserver/limited_stream.hpp"

using namespace coroserver;

template coro::awaitable<ReceiveBlockStatus> Stream::read_until(std::vector<char> &buffer, std::string_view , size_t max_size);
template coro::awaitable<ReceiveBlockStatus> Stream::read_block(std::vector<char> &buffer, size_t max_size);
using TestReadLine = decltype(std::declval<Stream>().read_until(std::declval<std::string&>(), "/r/n", 10000));


template class HandleHashMap<std::unique_ptr<int> >;

int main() {

}

