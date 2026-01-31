int* inc(int* this, int amount) -> *this += amount, this;

int main() {
    int x = 0;
    x.inc(1).inc(2).inc(3);
    if (x != 6) return 1;
    return 0;
}