#include <coroserver/prefixmap.h>
#include "check.h"

void test_find(coroserver::PrefixMap<int> &map, std::string_view path, std::initializer_list<int> list) {
    auto r = map.find(path);
    CHECK_EQUAL(r.size(), list.size());
    auto it1 = r.begin();
    auto it2 = list.begin();
    while (it1 != r.end() && it2 != list.end()) {
        CHECK_EQUAL((*it1)->payload, *it2);
        ++it1;
        ++it2;
    }


}

void test1() {

    coroserver::PrefixMap<int> map;

    map.insert("/aaa", 10);
    map.insert("/bbb", 20);
    map.insert("/bbb/ccc", 30);
    map.insert("/bbb/ddd", 40);
    map.insert("/abc", 50);
    map.insert("/abcd", 60);
    map.insert("/abcd/xyz", 70);
    map.insert("/abcd/xyw", 80);
    map.insert("/abcd/xyz/aaa", 90);


    test_find(map, "/123", {});
    test_find(map, "/aaa", {10});
    test_find(map, "/abc", {50});
    test_find(map, "/abc123", {50});
    test_find(map, "/abcd", {50,60});
    test_find(map, "/abcd/xyz/123", {50,60,70});
    test_find(map, "/abcd/xyw/123", {50,60,80});
    test_find(map, "/bbb/ccc/xxx", {20,30});



}

int main() {
    test1();
}
