#include "jitc.h"

#include <stdint.h>
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>

const char* skipped_tests[256] = {
    [10] = "goto not supported",
    [21] = "function params not implemented yet",
};

jmp_buf buffer;

bool script_segfault = false;

static void handle_segfault(int signal, siginfo_t* info, void* ptr) {
    printf("SEGFAULT\n");
    longjmp(buffer, 0);
}

int main(int argc, char** argv) {
    struct sigaction sa = {};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags     = SA_NODEFER;
    sa.sa_sigaction = handle_segfault;
    sigaction(SIGSEGV, &sa, NULL);
    int tests_to_run[argc - 1];
    for (int i = 1; i < argc; i++) {
        tests_to_run[i - 1] = atoi(argv[i]);
    }
    for (int i = 1; i <= 220; i++) {
        if (i != 33) continue;
        char path[256];
        sprintf(path, "c-testsuite/tests/single-exec/%05d.c", i);
        
        printf("Running test %d ... ", i);
        fflush(stdout);
        if (skipped_tests[i]) {
            printf("SKIPPED (%s)\n", skipped_tests[i]);
            continue;
        }
        jitc_context_t* context = jitc_create_context();
        jitc_error_t* error = NULL;
        if ((error = jitc_parse_file(context, path))) {
            printf("FAILED (compile error): ");
            jitc_report_error(error, stdout);
            jitc_destroy_context(context);
            continue;
        }
        int(*main_func)() = jitc_get(context, "main");
        if (!main_func) {
            printf("MISSING MAIN\n");
            jitc_destroy_context(context);
            continue;
        }
        int retval;
        bool segfault = false;
        if (setjmp(buffer)) segfault = true;
        else {
            script_segfault = true;
            retval = main_func();
            script_segfault = false;
        }
        if (!segfault && retval == 0) printf("PASSED\n");
        else {
            if (segfault && !script_segfault) {
                printf("Outside of script\n");
                continue;
            }
            script_segfault = false;
            if (!segfault) printf("FAILED (returned %d)\n", retval);
            uint32_t size = ((uint32_t*)main_func)[-1];
            printf("Machine code dump:\n");
            for (uint32_t i = 0; i < size; i++) {
                if (i != 0 && i % 16 == 0) fprintf(stderr, "\n");
                fprintf(stderr, "%02x ", ((uint8_t*)main_func)[i]);
            }
            fprintf(stderr, "\n");
        }
        jitc_destroy_context(context);
    }
    return 0;
}

