int fib(int x) {
    if (x <= 0) return x;
    return fib(x - 2) + fib(x - 1);
}

int main() {
    printf("%d\n", fib(5));
}
