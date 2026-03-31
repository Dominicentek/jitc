#include "../jitc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static struct {
    const char* name;
    const char* reason;
} skipped_tests[] = {
    { "tests/control-flow/009-continue.c", "continue not implemented yet" },
    { "tests/control-flow/010-switchcase.c", "switch not implemented yet" },
    { "tests/control-flow/011-fallthrough.c", "switch not implemented yet" },
    { "tests/control-flow/012-default.c", "switch not implemented yet" },
    { "tests/functions/007-varargs.c", "varargs not implemented yet"},
};

#ifdef _WIN32
struct _reent* _impure_ptr;
#endif

static int sort_string(const void* a, const void* b) {
    return strcmp(*(char**)a, *(char**)b);
}

static bool run_test(const char* name) {
    printf("Running test %s ... ", name);
    int(*main_func)();
    jitc_context_t* context = jitc_create_context();
    if (!jitc_parse_file(context, name) || !(main_func = jitc_get(context, "main"))) {
        printf("FAILED (compile error): ");
        jitc_report_error(context, stdout);
        jitc_destroy_context(context);
        return false;
    }
    int result = main_func();
    jitc_destroy_context(context);
    if (result != 0) printf("FAILED (returned %d)\n", result);
    else printf("PASSED\n");
    return result == 0;
}

int main(int argc, char** argv) {
    int total = 0, ran = 0, failed = 0;
    for (int i = 1; i < argc; i++) {
        total++;
        for (int j = 0; j < sizeof(skipped_tests) / sizeof(*skipped_tests); j++) {
            if (strcmp(argv[i], skipped_tests[j].name) == 0) {
                printf("Skipping test %s: %s\n", skipped_tests[j].name, skipped_tests[j].reason);
                goto skip;
            }
        }
        ran++;
        if (!run_test(argv[i])) failed++;
        skip:;
    }
    printf("Ran %d out of %d tests, %d failing (%.2f%% success rate, %.2f%% overall)\n", ran, total, failed, (1 - (float)failed / ran) * 100, (1 - (float)(failed + total - ran) / total) * 100);
    return 0;
}
