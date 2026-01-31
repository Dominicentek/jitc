void get_value(int* x) {
    if (x) *x = 0;
}

int main() {
    int x;
    get_value(nullptr);
    get_value(&x);
    return x;
}