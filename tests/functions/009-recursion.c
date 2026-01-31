int fact(int x) {
    if (x <= 1) return x;
    return x * fact(x - 1);
}

int main() {
    return fact(10) != 3628800;
}