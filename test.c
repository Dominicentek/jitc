#include "jitc.h"

#include <stdint.h>
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>

/*const char* skipped_tests[256] = {
    [10] = "goto not supported",
    [45] = "some weird pointer shit",
    [47] = "initializers not implemented yet",
    [48] = "initializers not implemented yet",
    [49] = "initializers not implemented yet",
    [50] = "initializers not implemented yet",
    [51] = "switch-case not implemented yet",
};

typedef struct {
    struct __attribute__((packed)) {
        char mov_rax[2];
        void* addr;
        char jmp_rax[2];
    };
    int size;
} jitc_func_trampoline_t;

jmp_buf buffer;

bool script_segfault = false;

static void handle_segfault(int signal, siginfo_t* info, void* ptr) {
    printf("SEGFAULT\n");
    longjmp(buffer, 0);
}

static void handle_interrupt(int signal, siginfo_t* info, void* ptr) {
    printf("INTERRUPTED\n");
    longjmp(buffer, 0);
}

static void handle_abort(int signal, siginfo_t* info, void* ptr) {
    printf("ABORTED\n");
    longjmp(buffer, 0);
}

static void handle_fperror(int signal, siginfo_t* info, void* ptr) {
    printf("FP ERROR\n");
    longjmp(buffer, 0);
}

static void handle_signal(int signal, void(*func)(int, siginfo_t*, void*)) {
    struct sigaction sa = {};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;
    sa.sa_sigaction = func;
    sigaction(signal, &sa, NULL);
}

int main(int argc, char** argv) {
    handle_signal(SIGSEGV, handle_segfault);
    handle_signal(SIGINT, handle_interrupt);
    handle_signal(SIGABRT, handle_abort);
    handle_signal(SIGFPE, handle_fperror);

    int tests_to_run[argc - 1];
    for (int i = 1; i < argc; i++) {
        tests_to_run[i - 1] = atoi(argv[i]);
    }
    for (int i = 1; i <= 220; i++) {
        if (argc > 1) {
            bool run_test = false;
            for (int j = 0; j < argc - 1 && !run_test; j++) {
                if (tests_to_run[j] == i) run_test = true;
            }
            if (!run_test) continue;
        }
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
        jitc_create_header(context, "stdio.h", "int printf(const char*, ...);");
        jitc_create_header(context, "stdlib.h", "void* calloc(unsigned long a, unsigned long b);");
        int(*main_func)();
        if (!jitc_parse_file(context, path) || !(main_func = jitc_get(context, "main"))) {
            printf("FAILED (compile error): ");
            jitc_report_error(context, stdout);
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
            jitc_func_trampoline_t* func = (void*)jitc_get(context, "foo");
            printf("Machine code dump:\n");
            for (uint32_t i = 0; i < func->size; i++) {
                if (i != 0 && i % 16 == 0) fprintf(stderr, "\n");
                fprintf(stderr, "%02x ", ((uint8_t*)func->addr)[i]);
            }
            fprintf(stderr, "\n");
        }
        jitc_destroy_context(context);
    }
    return 0;
}*/

int main() {
    jitc_context_t* context = jitc_create_context();
    jitc_parse(context, "preserve int main() { return 1; }", NULL);
    int(*main_func)() = jitc_get(context, "main");
    printf("returned %d\n", main_func());
    jitc_parse(context, "int main() { return 2; }", NULL);
    printf("returned %d\n", main_func());
    return 0;
}
