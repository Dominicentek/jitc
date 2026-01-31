int inc(int* this) -> (*this)++;

int main() {
    int x = 0;
    x.inc();
    if (x != 1) return 1;
    return 0;
}