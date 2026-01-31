int main() {
    float a = 1, b = 2;
    if (!(a < b)) return 1;
    if (!(-a < -b)) return 2;
    return 0;
}