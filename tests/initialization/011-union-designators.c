int main() {
    union {
        float f;
        int x, y, z;
    } u = { .x = 1, 2, .f = 1.5f, 3 };
    if (u.x != 0x3fc00000) return 1;
    return 0;
}