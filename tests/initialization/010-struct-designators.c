int main() {
    struct {
        int x, y, z;
    } s = { .y = 1, 2, .x = 3 };
    if (s.x != 3) return 1;
    if (s.y != 1) return 2;
    if (s.z != 2) return 3;
    return 0;
}