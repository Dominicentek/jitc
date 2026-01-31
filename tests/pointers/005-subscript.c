int main() {
    int x = 0;
    int* p = &x;
    
    if (p[0] != 0) return 1;
    if (0[p] != 0) return 2;
    return 0;
}