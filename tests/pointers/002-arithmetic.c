int main() {
    int* p1;
    int* p2 = p1 + 2;
    if ((unsigned long)p2 - (unsigned long)p1 == 8) return 0;
    return 1;
}