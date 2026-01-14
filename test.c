#include "jitc.h"

#include <sys/inotify.h>
#include <unistd.h>

#define NAME "test.txt"
#define ms *1000

int main() {
    bool error = false;
    jitc_context_t* context = jitc_create_context();
    jitc_create_header(context, "stdio.h", "int printf(const char*, ...);");
    if (!jitc_parse_file(context, NAME)) error = true;
    void(*func)() = error ? NULL : jitc_get(context, "func");
    int notify = inotify_init1(IN_NONBLOCK);
    char buf[4096];
    inotify_add_watch(notify, NAME, IN_MODIFY | IN_DONT_FOLLOW);
    while (true) {
        int len = read(notify, buf, sizeof(buf));
        if (len > 0) {
            struct inotify_event* event = (struct inotify_event*)buf;
            if (!jitc_parse_file(context, "test.txt")) {
                jitc_report_error(context, stderr);
                error = true;
            }
            else {
                if (!func) func = jitc_get(context, "func");
                error = false;
            }
        }
        if (!error) func();
        usleep(100 ms);
    }
    return 0;
}
