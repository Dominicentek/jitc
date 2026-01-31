int main() {
    int x = 0;
    int* p = &x;
    void* v = p;
    if (*(int*)v == x) return 0;
    return 1;
}