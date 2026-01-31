int main() {
    int* p1;
    int* p2 = p1;
    p2++;
    if (p1 + 1 == p2) return 0;
    return 1;
}