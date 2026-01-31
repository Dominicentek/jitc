int inc(int* this) -> (*this)++;

int main() {
    int x = 0;
    int(*ptr)(int*) = x.inc;
    ptr(&x);
    if (x != 1) return 1;
    return 0;
}