int main() {
    union {
        int x, y, z;
    } u = {1, 2, 3};
    if (u.x != 1) return 1;
    return 0;
}