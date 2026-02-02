<T> unsigned long size() -> sizeof(T);

int printf(const char*, ...);

int main() {
    if (size<int>() != sizeof(int)) return 1;
    if (size<long>() != sizeof(long)) return 2;
    return 0;
}
