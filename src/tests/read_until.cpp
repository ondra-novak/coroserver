#include "check.h"
#include <coroserver/stream.h>
#include <coroserver/stream_utils.h>
#include <queue>
#include "test_stream.h"

template<unsigned int N>
void test(std::vector<std::string> zadani, const coroserver::kmp_pattern<char,N> &sep, std::size_t limit, bool r1, std::string_view r2) {
    coroserver::Stream s (std::make_shared<TestStream<100>>(std::move(zadani)));
    coroserver::ReadUntil rdline(s, limit);
    auto r = rdline(sep, true);
    CHECK_EQUAL(r.has_value(),r1);
    if (r.has_value()) {
        CHECK_EQUAL(r.get(), r2);
        std::string_view extra = s.read_nb();
        CHECK_EQUAL("Extra data",extra);
    }
}

constexpr coroserver::kmp_pattern nlnl("\r\n\r\n");
constexpr coroserver::kmp_pattern srch("ahoj");


int main() {
    coroserver::kmp_search st ( srch);
    std::string_view tst = "ahohojahoahojaho";
    for (std::size_t i = 0; i < tst.length(); ++i) {
        auto res = st(tst[i]);
        if (res) std::cout << "Found at: " << i << std::endl;
    }



    test({"Test line1\n\rTest line2\r\n\r\nExtra data"},nlnl,9999,true,"Test line1\n\rTest line2\r\n\r\n");
    test({"Test line1\n\rTest line2\r\n","\r\nExtra data"},nlnl,9999,true,"Test line1\n\rTest line2\r\n\r\n");
    test({"Test line1\n\rTest line2\r\n","\r","\nExtra data"},nlnl,9999,true,"Test line1\n\rTest line2\r\n\r\n");
    test({"Test line1\n\r","Test line2\r\n\r\n","Extra data"},nlnl,15,false,{});
    test({"Test line1\n\r","Test line2\r\n\r\n","Extra data"},nlnl,5,false,{});
}

