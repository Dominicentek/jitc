#define X

#ifdef X
#define Y 0
#else
#define Y 1
#endif

#ifndef Y
int main() {
    return 2;
}
#else
int main() {
    return Y;
}
#endif