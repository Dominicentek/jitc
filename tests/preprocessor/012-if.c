#if 0
ERROR
#endif

#if (2 * 3) - 6
ERROR
#endif

#define X

#if defined(X) + defined(X) != 2
ERROR
#endif

#undef X
#define Y

#if defined(X)
ERROR
#elif defined(Y)
int main() {
    return 0;
}
#else
ERROR
#endif