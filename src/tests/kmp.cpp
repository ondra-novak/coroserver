#include "check.h"
#include <coroserver/substring.h>

void test1() {
    std::cout << "test1" << std::endl;
    coroserver::SubstringSearch<char> kmp("\r\n\r\n");
    bool a1 = kmp.append("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r");
    bool a2 = kmp.append("\n\r\nTest");
    CHECK_EQUAL(a1, false);
    CHECK_EQUAL(a2, true);
    CHECK_EQUAL(kmp.get_global_pos(), 41);
    CHECK_EQUAL(kmp.get_fragment_pos(), -1);
    CHECK_EQUAL(kmp.get_pos_after_pattern(), 3);


}

void test2() {
    std::cout << "test2" << std::endl;
    coroserver::SubstringSearch<char> kmp("\r\n\r\n");
    bool a1 = kmp.append("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\n");
    CHECK_EQUAL(a1, true);
    CHECK_EQUAL(kmp.get_global_pos(), 41);
    CHECK_EQUAL(kmp.get_fragment_pos(), 41);
    CHECK_EQUAL(kmp.get_pos_after_pattern(), 45);
}

void test3() {
    std::cout << "test3" << std::endl;
    coroserver::SubstringSearch<char> kmp("\r\n\r\n");
    bool a1 = kmp.append("HTTP/1.0 200 OK\r\nContent-Type: text/plain");
    bool a2 = kmp.append("\r");
    bool a3 = kmp.append("\n");
    bool a4 = kmp.append("\r");
    bool a5 = kmp.append("\n");
    CHECK_EQUAL(a1, false);
    CHECK_EQUAL(a2, false);
    CHECK_EQUAL(a3, false);
    CHECK_EQUAL(a4, false);
    CHECK_EQUAL(a5, true);
    CHECK_EQUAL(kmp.get_global_pos(), 41);
    CHECK_EQUAL(kmp.get_fragment_pos(), -3);
    CHECK_EQUAL(kmp.get_pos_after_pattern(), 1);
}


void test4() {
    std::cout << "test4" << std::endl;
    coroserver::SubstringSearch<char> kmp("\r\n\r\n");
    bool a1 = kmp.append("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r");
    bool a2 = kmp.append("\n\r\nTest");
    CHECK_EQUAL(a1, false);
    CHECK_EQUAL(a2, true);
    CHECK_EQUAL(kmp.get_global_pos(), 41);
    CHECK_EQUAL(kmp.get_fragment_pos(), -1);
    CHECK_EQUAL(kmp.get_pos_after_pattern(), 3);
}

void test5() {
    std::cout << "test5" << std::endl;
    coroserver::SubstringSearch<char> kmp("x");
    bool a1 = kmp.append("abceiwdlwcwcwedqpwqskq");
    bool a2 = kmp.append("wepokdpo2xwwe[pdxwieew");
    CHECK_EQUAL(a1, false);
    CHECK_EQUAL(a2, true);
    CHECK_EQUAL(kmp.get_global_pos(), 31);
    CHECK_EQUAL(kmp.get_fragment_pos(), 9);
    CHECK_EQUAL(kmp.get_pos_after_pattern(), 10);

}

void test6() {
    std::cout << "test6" << std::endl;
    CHECK_EXCEPTION(std::invalid_argument, coroserver::SubstringSearch<char> kmp(""));
}

int main() {
    test1();
    test2();
    test3();
    test4();
    test5();
    test6();


}
