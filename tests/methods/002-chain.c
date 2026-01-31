int* inc(int* this) -> (*this)++, this;

int main() {
    int x = 0;
    x.inc().inc().inc();
    if (x != 3) return 1;
    return 0;
}