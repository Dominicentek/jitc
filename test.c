#include "jitc.h"

int main() {
    jitc_context_t* context = jitc_create_context();
    if (!jitc_parse(context, "extern int x;", NULL)) goto error;
    if (!jitc_parse(context, "int main() { return x; }", NULL)) goto error;
    if (!jitc_parse(context, "int x = 1;", NULL)) goto error;
    int(*main_func)() = jitc_get(context, "main");
    if (!main_func) goto error;
    printf("%d\n", main_func());
    return 0;
    error:
    jitc_report_error(context, stderr);
    return 1;
}
