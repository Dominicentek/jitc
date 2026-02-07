int deref(int* ptr) {
    return *ptr;
}

int main() {
    if (deref(&(int){3}) != 3) return 1;
    if (deref((int[]){1, 2, 3, 4}) != 1) return 2;
    return 0;
}