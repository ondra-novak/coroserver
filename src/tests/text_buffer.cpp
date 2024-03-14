#include "../coroserver/strutils.h"

#include "check.h"

#include <iostream>

using Txt = coroserver::DataBuffer<char, 40>;

constexpr Txt test1 = {};
constexpr Txt test2 = {"ahoj svete"};

int main() {

    CHECK_EQUAL(test1, "");
    CHECK_EQUAL(test2, "ahoj svete");
    std::cout << sizeof(coroserver::DataBuffer<char, 16>) << std::endl;
    std::cout << sizeof(coroserver::DataBuffer<char, 24>) << std::endl;
    std::cout << sizeof(coroserver::DataBuffer<char, 32>) << std::endl;
    std::cout << sizeof(coroserver::DataBuffer<char, 40>) << std::endl;
    std::cout << sizeof(std::string) << std::endl;
    Txt n("ahoj");
    std::string_view t = n;
    CHECK_EQUAL(t, "ahoj");
    Txt n1("aqpwodkpqkwposkqpwokspqwomqpwjdpqnmxoqnxcdoqwndsoqwispw     qw[ psl[    pq p[qwd,mqpowedm qpwdmqwdqwpdqhoj");
    std::string_view t1 = n1;
    CHECK_EQUAL(t1, "aqpwodkpqkwposkqpwokspqwomqpwjdpqnmxoqnxcdoqwndsoqwispw     qw[ psl[    pq p[qwd,mqpowedm qpwdmqwdqwpdqhoj");
    Txt n2(n1);
    std::string_view t2= n1;
    CHECK_EQUAL(t2, "aqpwodkpqkwposkqpwokspqwomqpwjdpqnmxoqnxcdoqwndsoqwispw     qw[ psl[    pq p[qwd,mqpowedm qpwdmqwdqwpdqhoj");
    Txt n3;
    n3 = n;
    std::string_view t3 = n3;
    CHECK_EQUAL(t3, "ahoj");
    n2 = n;
    t2 = n2;
    CHECK_EQUAL(t2, "ahoj");



}
