#include "check.h"
#include "test_stream.h"
#include <coroserver/limited_stream.h>


void test1() {
    std::string result;
    std::string expected = "0123456789ABCDEF";

    auto s = TestStream<100>::create({}, &result);
    auto chs = coroserver::LimitedStream::write(s,16);
    chs.write("0123456789").wait();
    chs.write("A").wait();;
    bool ret = chs.write("BCDEF").wait();
    CHECK(ret);
    ret = chs.write("GHIJ").wait();
    CHECK(!ret);
    chs.write_eof().wait();;
    CHECK_EQUAL(result, expected);
}

void test2() {
    std::string result;
    std::string expected("0123456789\0\0\0\0\0",16);

    auto s = TestStream<100>::create({}, &result);
    auto chs = coroserver::LimitedStream::write(s,16);
    chs.write("0123456789").wait();
    bool ret = chs.write_eof().wait();
    CHECK(ret);

    CHECK(result.compare(expected) == 0);
}


void test3() {
    auto s = TestStream<100>::create({"Hello", " World", " Extra data"});
    auto ls = coroserver::LimitedStream::read(s, 11);

    std::string buff;
    bool r = ls.read_block(buff, 15).wait();
    CHECK(!r);
    CHECK_EQUAL(buff,"Hello World");
    r = s.read_block(buff, 100).wait();
    CHECK(!r);
    CHECK_EQUAL(buff," Extra data");
}

void test4() {
    auto s = TestStream<100>::create({"Hello World Extra data"});
    auto ls = coroserver::LimitedStream::read(s, 11);

    std::string buff;
    bool r = ls.read_block(buff, 5).wait();
    CHECK(r);
    CHECK_EQUAL(buff,"Hello");
    r = ls.read_block(buff, 15).wait();
    CHECK(!r);
    CHECK_EQUAL(buff," World");
    r = s.read_block(buff, 100).wait();
    CHECK(!r);
    CHECK_EQUAL(buff," Extra data");
}



int main() {
    test1();
    test2();
    test3();
    test4();





}
/*
 * limited_stream.cpp
 *
 *  Created on: 16. 4. 2023
 *      Author: ondra
 */




