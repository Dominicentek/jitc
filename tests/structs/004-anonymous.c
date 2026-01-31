typedef struct {
    struct {
        struct {
            struct {
                int x;
            } c;
        } b;
    } a;
} str;

int main() {
    if (sizeof(str) != 4) return 1;
    str s;
    s.a.b.c.x = 0;
    int* p = (int*)&s;
    return *p;
}