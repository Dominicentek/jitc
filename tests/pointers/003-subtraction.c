int main() {
    int* p1;
    int* p2 = p1 + 2;
    if (p2 - p1 == 2) return 0;
    return 1;
}