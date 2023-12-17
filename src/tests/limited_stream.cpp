#include "check.h"
#include "test_stream.h"
#include <coroserver/limited_stream.h>
#include <coroserver/stream_utils.h>


void test1() {
    std::string result;
    std::string expected = "0123456789ABCDEF";

    auto s = TestStream<100>::create({}, &result);
    auto chs = coroserver::LimitedStream::write(s,16);
    chs.write("0123456789").wait();
    chs.write("A").wait();;
    bool ret = chs.write("BCDEF").get();
    CHECK(ret);
    ret = chs.write("GHIJ").get();
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
    bool ret = chs.write_eof().get();
    CHECK(ret);

    CHECK(result.compare(expected) == 0);
}


void test3() {
    auto s = TestStream<100>::create({"Hello", " World", " Extra data"});
    auto ls = coroserver::LimitedStream::read(s, 11);


    coroserver::BlockReader blk(ls);
    coroserver::BlockReader org_blk(s);
    std::string_view buff = blk.read(15).get();
    CHECK_EQUAL(buff,"Hello World");
    buff = org_blk.read(100).get();
    CHECK_EQUAL(buff," Extra data");
}

void test4() {
    auto s = TestStream<100>::create({"Hello World Extra data"});
    auto ls = coroserver::LimitedStream::read(s, 11);

    std::string_view buff;
    coroserver::BlockReader blk(ls);
    coroserver::BlockReader org_blk(s);
    buff =  blk.read(5).get();
    CHECK_EQUAL(buff,"Hello");
    buff = blk.read(15).get();
    CHECK_EQUAL(buff," World");
    buff = org_blk.read(100).get();
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




