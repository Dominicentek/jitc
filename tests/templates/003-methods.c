<T> T inc(T* this) -> (*this)++;

int main() {
    int x = 0;
    x.inc<int>();
    if (x != 1) return 1;
    return 0;
}