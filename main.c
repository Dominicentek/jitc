#include "jitc.h"

int main() {
    jitc_context_t* context = jitc_create_context();
    jitc_parse_file(context, "test/test.c");
    int(*main_func)() = jitc_get(context, "main");
    int value = main_func();
    printf("Returned %d\n", value);
    jitc_destroy_context(context);
    return 0;
}
