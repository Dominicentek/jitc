int main() {
    struct {
        int x, y, z;
    } s = {1, 2, 3};
    if (s.x != 1) return 1;
    if (s.y != 2) return 2;
    if (s.z != 3) return 3;
    return 0;
}