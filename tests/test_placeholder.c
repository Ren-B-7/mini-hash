#include <stdio.h>
#include "include/minunit.h"
int tests_run = 0;
static char* test_placeholder(void) { mu_assert("placeholder", 1 == 1); return 0; }
static char* all_tests(void) { mu_run_test(test_placeholder); return 0; }
int main(void) { char* res = all_tests(); if (res) printf("%s\n", res); else printf("ALL TESTS PASSED\n"); return res != 0; }
