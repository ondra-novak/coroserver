#include "check.h"
#include <coroserver/stream.h>
#include <queue>
#include "test_stream.h"

void test(std::vector<std::string> zadani, std::string_view sep, std::size_t limit, bool r1, std::string_view r2) {
    coroserver::Stream s (std::make_shared<TestStream<100>>(std::move(zadani)));
    std::string buff;
    bool r = s.read_until(buff, sep, limit).join();
    CHECK_EQUAL(r,r1);
    if (r) {
        CHECK_EQUAL(buff, r2);
        std::string_view extra = s.read_nb();
        CHECK_EQUAL("Extra data",extra);
    }
}

constexpr coroserver::search_kmp<10> srch("ahoj");


int main() {
    coroserver::search_kmp<10>::State st = {};
    std::string_view tst = "ahohojahoahojaho";
    for (std::size_t i = 0; i < tst.length(); ++i) {
        auto res = srch(st, tst[i]);
        if (res) std::cout << "Found at: " << i << std::endl;
    }


    test({"Test line1\n\rTest line2\r\n\r\nExtra data"},"\r\n\r\n",9999,true,"Test line1\n\rTest line2\r\n\r\n");
    test({"Test line1\n\rTest line2\r\n","\r\nExtra data"},"\r\n\r\n",9999,true,"Test line1\n\rTest line2\r\n\r\n");
    test({"Test line1\n\rTest line2\r\n","\r","\nExtra data"},"\r\n\r\n",9999,true,"Test line1\n\rTest line2\r\n\r\n");
    test({"Test line1\n\rTest line2\r\n"},"\r\n\r\n",9999,false,{});
    test({"Test line1\n\r","Test line2\r\n\r\n","Extra data"},"\r\n\r\n",15,true,"Test line1\n\rTest line2\r\n\r\n");
    test({"Test line1\n\r","Test line2\r\n\r\n","Extra data"},"\r\n\r\n",5,false,{});
}

