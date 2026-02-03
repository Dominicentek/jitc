#include "../jitc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

struct {
    const char* name;
    const char* reason;
} skipped_tests[] = {
    { "tests/control-flow/009-continue.c", "continue not implemented yet" },
    { "tests/control-flow/010-switchcase.c", "switch not implemented yet" },
    { "tests/control-flow/011-fallthrough.c", "switch not implemented yet" },
    { "tests/control-flow/012-default.c", "switch not implemented yet" },
    { "tests/floats/005-inf.c", "requires compound literals" },
    { "tests/floats/006-nan.c", "requires compound literals" },
    { "tests/functions/007-varargs.c", "varargs not implemented yet"},
    { "tests/variables/005-shadow.c", "variable shadowing not implemented yet" },
};

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

static void test_directory(const char* dirname, int* total, int* ran, int* failed) {
    int count = 0;
    DIR* dir = opendir(dirname);
    struct dirent* dirent;
    while ((dirent = readdir(dir))) {
        if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0) continue;
        if (dirent->d_type != DT_DIR && dirent->d_type != DT_REG) continue;
        count++;
    }
    closedir(dir);
    char* files[count];
    dir = opendir(dirname);
    count = 0;
    while ((dirent = readdir(dir))) {
        if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0) continue;
        if (dirent->d_type != DT_DIR && dirent->d_type != DT_REG) continue;
        char name[PATH_MAX];
        snprintf(name, PATH_MAX, "%s%s%s", dirname, dirent->d_name, dirent->d_type == DT_DIR ? "/" : "");
        files[count++] = strdup(name);
    }
    qsort(files, count, sizeof(char*), sort_string);
    for (int i = 0; i < count; i++) {
        if (files[i][strlen(files[i]) - 1] == '/') test_directory(files[i], total, ran, failed);
        else {
            (*total)++;
            for (int j = 0; j < sizeof(skipped_tests) / sizeof(*skipped_tests); j++) {
                if (strcmp(files[i], skipped_tests[j].name) == 0) {
                    printf("Skipping test %s: %s\n", files[i], skipped_tests[j].reason);
                    goto skipped;
                }
            }
            (*ran)++;
            if (!run_test(files[i])) (*failed)++;
        }
        skipped:
        free(files[i]);
    }
}

int main(int argc, char** argv) {
    int total = 0, ran = 0, failed = 0;
    if (argc == 1)
        test_directory("tests/", &total, &ran, &failed);
    else for (int i = 1; i < argc; i++) {
        total++; ran++;
        if (!run_test(argv[i])) failed++;
    }
    printf("Ran %d out of %d tests, %d failing (%.2f%% success rate, %.2f%% overall)\n", ran, total, failed, (1 - (float)failed / ran) * 100, (1 - (float)(failed + total - ran) / total) * 100);
    return 0;
}
