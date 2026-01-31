int main() {
    int x = (lambda(): int -> 1)() + (lambda(): int -> 2)();
    if (x != 3) return 1;
    return 0;
}