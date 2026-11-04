#include "test_harness.hpp"

#include <iostream>

void test_cli();
void test_header();
void test_record();
void test_bam_slim();

int main() {
    test_cli();
    test_header();
    test_record();
    test_bam_slim();

    if (husk_test_failures == 0) {
        std::cout << "all unit tests passed\n";
        return 0;
    }
    std::cerr << husk_test_failures << " unit test assertion(s) failed\n";
    return 1;
}
