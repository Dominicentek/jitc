int main() {
    struct {
        int x;
        struct {
            int y;
        };
    } s = { 1, { .y = 2 }};
    if (s.x != 1) return 1;
    if (s.y != 2) return 2;
    return 0;
}