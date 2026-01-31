int main() {
    int x[1];
    if (x != &x) return 1;
    if (main != &main) return 2;
    return 0;
}