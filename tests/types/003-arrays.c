int main() {
    int a[4];
    int b[4][4];
    int* c[4];
    int(*d)[4];
    int(*(*e[2])[4][4]);
    return sizeof(b) - 64;
}