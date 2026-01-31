int value(int* this) -> 1;
int value(long* this) -> 2;
int value(unsigned long* this) -> 3;

<T> int get() {
    T x;
    return x.value();
}

int main() {
    if (get<int>() != 1) return 1;
    if (get<long>() != 2) return 2;
    if (get<unsigned long>() != 3) return 3;
    return 0;
}