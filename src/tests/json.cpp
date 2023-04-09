#include "check.h"

#include <coroserver/json/value.h>
#include <coroserver/json/parser.h>
#include <coroserver/json/serializer.h>
#include "test_stream.h"

cocls::async<void> test1() {
    std::string out;
    coroserver::Stream s = TestStream<100>::create({
        "[1,2,3,{\"a\":10,\"b\":\"ahoj čau\"}",
        ",",
        "true,false,null,{\"z\":{\"x\":[[1],2]}}]"
    }, &out);

    coroserver::json::Value v = co_await coroserver::json::parse(s);
    CHECK_EQUAL(v[0].get_int(),1);
    CHECK_EQUAL(v[1].get_int(),2);
    CHECK_EQUAL(v[2].get_int(),3);
    CHECK_EQUAL(v[3]["a"].get_int(),10);
    CHECK_EQUAL(v[3]["b"].get_string_view(),"ahoj čau");
    CHECK_EQUAL(v[4].get_bool(),true);
    CHECK_EQUAL(v[5].get_bool(),false);
    CHECK_EQUAL(v[6].is_null(),true);



    std::string chk = R"json([1,2,3,{"a":10,"b":"ahoj \u010dau"},true,false,null,{"z":{"x":[[1],2]}}])json";
    co_await coroserver::json::serialize(s, v);
    CHECK_EQUAL(chk,out);

}

void test2() {
    std::string chk = R"json([1,2,3,{"a":10,"b":"ahoj \u010dau"},true,false,null,{"z":{"x":[[1],2]}}])json";
    coroserver::json::Value v = coroserver::json::from_string(chk);
    CHECK_EQUAL(v[0].get_int(),1);
    CHECK_EQUAL(v[1].get_int(),2);
    CHECK_EQUAL(v[2].get_int(),3);
    CHECK_EQUAL(v[3]["a"].get_int(),10);
    CHECK_EQUAL(v[3]["b"].get_string_view(),"ahoj čau");
    CHECK_EQUAL(v[4].get_bool(),true);
    CHECK_EQUAL(v[5].get_bool(),false);
    CHECK_EQUAL(v[6].is_null(),true);

    std::string chk2 = v.to_string();
    CHECK_EQUAL(chk,chk2);

}


int main() {
    test1().join();
    test2();
}
