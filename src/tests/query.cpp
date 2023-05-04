#include "check.h"

#include <coroserver/http_common.h>


struct Data{
    int a = 0;
    float b;
    double c;
    char d;
    std::string e;
    bool f;
    std::optional<unsigned int> g;
    std::optional<float> h;
    std::optional<double> i;
    std::optional<char> j;
    std::optional<std::string> k;
    std::optional<bool> l;

    static constexpr auto fldmap = coroserver::http::makeQueryFieldMap<Data>({
        {"a", &Data::a},
        {"b", &Data::b},
        {"c", &Data::c},
        {"d", &Data::d},
        {"e", &Data::e},
        {"f", &Data::f},
        {"g", &Data::g},
        {"h", &Data::h},
        {"i", &Data::i},
        {"j", &Data::j},
        {"k", &Data::k},
        {"l", &Data::l}
    });
};



void test1() {

    Data ret;
    std::size_t cnt = coroserver::http::parse_query("path?a=10&unknown=value&b=20.3&c=1.23e04&d=A&e=hello%20world&f&g=42&h=56&i=0.00001&j=B&k=TestTest&l=false", Data::fldmap, ret);
    CHECK_EQUAL(cnt, 12);
    CHECK_EQUAL(ret.a,10);
    CHECK_BETWEEN(20.29,ret.b,20.31);
    CHECK_BETWEEN(1.229e4,ret.c,1.2301e4);
    CHECK_EQUAL(ret.d,'A');
    CHECK_EQUAL(ret.e,"hello world");
    CHECK(ret.f);
    CHECK_EQUAL(*ret.g,42);
    CHECK_EQUAL(*ret.h,56);
    CHECK_EQUAL(*ret.i,0.00001);
    CHECK_EQUAL(*ret.j,'B');
    CHECK_EQUAL(*ret.k,"TestTest");
    CHECK(!*ret.l);

}

void test2() {
    Data v;
    coroserver::http::parse_query("path?a=10&unknown=value&b=20.3&c=1.23e04&d=A&e=hello%20world&f&g=42&j=B&k=TestTest&l=false", Data::fldmap, v);
    std::string out;
    coroserver::http::build_query(v, Data::fldmap, [&](char c){out.push_back(c);});
    CHECK_EQUAL(out, "a=10&b=20.299999&c=12300.000000&d=65&e=hello%20world&f=1&g=42&j=66&k=TestTest&l=0");
}

int main() {
    test1();
    test2();
    return 0;
}
