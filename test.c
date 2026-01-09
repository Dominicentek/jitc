#include "jitc.h"

#include <unistd.h>
#include <sys/inotify.h>

#define ms *1000
#define SOURCE "file.txt"

int main() {
    jitc_context_t* context = jitc_create_context();
    if (!jitc_parse_file(context, SOURCE)) {
        jitc_report_error(context, stderr);
        return 1;
    }
    void(*func)() = jitc_get(context, "func");
    int notif = inotify_init1(IN_NONBLOCK);
    char buf[4096];
    inotify_add_watch(notif, SOURCE, IN_MODIFY | IN_DONT_FOLLOW);
    while (true) {
        usleep(100 ms);
        func();

        int len = read(notif, buf, sizeof(buf));
        if (len < 1) continue;
        struct inotify_event* event = (struct inotify_event*)buf;
        if (!(event->mask & IN_MODIFY)) continue;
        if (!jitc_parse_file(context, SOURCE)) jitc_report_error(context, stderr);
    }
}

