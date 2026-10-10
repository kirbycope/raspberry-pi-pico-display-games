#include <stdlib.h>
#include <string.h>

#include "test.h"

#define MAX_TESTS 512

static struct { const char *name; void (*fn)(void); } tests[MAX_TESTS];
static int count, failures, current_failed;

void test_register(const char *name, void (*fn)(void)) {
    if (count < MAX_TESTS) tests[count].name = name, tests[count].fn = fn, count++;
}

void test_fail(const char *file, int line, const char *expr) {
    printf("  FAIL %s:%d: %s\n", file, line, expr);
    current_failed = 1;
}

int main(int argc, char **argv) {
    // An argument runs only the tests whose name contains it: pdg_tests tetris
    const char *filter = argc > 1 ? argv[1] : NULL;
    int run = 0, failed_tests = 0;
    for (int i = 0; i < count; i++) {
        if (filter && !strstr(tests[i].name, filter)) continue;
        current_failed = 0;
        tests[i].fn();
        run++;
        if (current_failed) {
            failed_tests++;
            printf("FAIL %s\n", tests[i].name);
        }
    }
    failures = failed_tests;
    printf("%d tests, %d failed\n", run, failures);
    return failures ? 1 : 0;
}
