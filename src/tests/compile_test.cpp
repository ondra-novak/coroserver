#include "../coroserver/stream.h"

using namespace coroserver;

template awaitable<ReceiveUntilStatus<std::vector<char>, 10> > Stream::receive_until(std::vector<char> &buffer, const char (&sep)[10], size_t max_size);
template awaitable<ReceiveBlockStatus<std::vector<char>> > Stream::receive_block(std::vector<char> &buffer, size_t max_size);
using TestReadLine = decltype(std::declval<Stream>().receive_until(std::declval<std::string&>(), "/r/n", 10000));


int main() {

}

