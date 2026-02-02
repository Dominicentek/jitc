#include "stddef.h"

typedef struct {
    int a;
    union {
        struct {
            int b;
            union {
                struct {
                    char c;
                    short d;
                };
                int e[2];
            };
            struct {
                float f;
                struct {
                    double g;
                    char h[3];
                };
            };
        };
        double i;
    };
    struct {
        int j[2];
        union {
            struct {
                char k;
                struct {
                    short l;
                    int m;
                };
            };
            long n;
        };
    };
} torture;

typedef unsigned long size_t;

int main() {
    if (offsetof(torture, a) != 0) return 1;
    if (offsetof(torture, b) != 8) return 2;
    if (offsetof(torture, c) != 12) return 3;
    if (offsetof(torture, d) != 14) return 4;
    if (offsetof(torture, e) != 12) return 5;
    if (offsetof(torture, f) != 24) return 6;
    if (offsetof(torture, g) != 32) return 7;
    if (offsetof(torture, h) != 40) return 8;
    if (offsetof(torture, i) != 8) return 9;
    if (offsetof(torture, j) != 48) return 10;
    if (offsetof(torture, k) != 56) return 11;
    if (offsetof(torture, l) != 60) return 12;
    if (offsetof(torture, m) != 64) return 13;
    if (offsetof(torture, n) != 56) return 14;

    return 0;
}
