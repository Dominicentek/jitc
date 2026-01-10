#include "jitc.h"

int main() {
    jitc_context_t* context = jitc_create_context();
    if (!jitc_parse_file(context, "file.txt")) {
        jitc_report_error(context, stderr);
        return 1;
    }
    int(*func)() = jitc_get(context, "main");
    return func();
}

