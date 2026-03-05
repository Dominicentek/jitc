int main() {
    int x = 3;
    int* p = &x;
    *p = 0;
    return x;
}