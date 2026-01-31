#define SHIFT(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, ...) p
#define COUNT(...) SHIFT(__VA_ARGS__ __VA_OPT__(,) 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

int main() {
    return COUNT(a, b, c, d, e, f) - (COUNT(a, b, c, d) + COUNT(a, b)) + COUNT();
}