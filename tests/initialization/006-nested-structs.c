int main() {
    struct {
        int x;
        struct {
            int y;
            struct {
                int z;
            };
        };
    } s1 = {1, 2, 3}, s2 = {1, {2, {3}}}, s3 = {1, {{2, 3}}};
    if (s1.x != s2.x) return 1;
    if (s1.y != s2.y) return 2;
    if (s1.z != s2.z) return 3;
    if (s3.z != 0) return 3;
    return 0;
}