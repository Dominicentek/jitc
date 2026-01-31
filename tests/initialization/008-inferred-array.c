int main() {
    int x[] = {1, 2, {3, 4}, 5};
    if (sizeof(x) != 16) return 1;
    return 0;
}