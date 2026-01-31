int main() {
    int x[] = { [2] = 3 };
    if (x[2] != 3) return 1;
    if (sizeof(x) != 12) return 2;
    return 0;
}