<T> T* inc(T* this) -> (*this)++, this;

int main() {
    int x = 0;
    x.inc<int>().inc<int>().inc<int>();
    if (x != 3) return 1;
    return 0;
}