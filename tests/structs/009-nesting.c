#include "stdio.h"

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

int main() {
    torture t;

    t.b = 42;
    t.c = 'x';
    t.d = 123;
    t.e[1] = 999;
    t.f = 3.14f;
    t.g = 2.718;
    t.h[0] = 'a';
    t.j[0] = 11;
    t.k = 'z';
    t.l = 77;
    t.m = 888;
    t.n = 123456789L;

    printf("t.a=%d, t.b=%d, t.c=%c, t.d=%d, t.e[1]=%d, t.f=%f, t.g=%f\n",
        t.a, t.b, t.c, t.d, t.e[1], t.f, t.g
    );
    printf("t.h[0]=%c, t.j[0]=%d, t.k=%c, t.l=%d, t.m=%d, t.n=%ld\n",
        t.h[0], t.j[0], t.k, t.l, t.m, t.n
    );

    return 0;
}