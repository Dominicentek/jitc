<T> T min(T a, T b) -> a < b ? a : b;

int main() {
    if (min<int>(3, 5) != 3) return 1;
    if (min<float>(3, 5) != 3.0f) return 2;
    return 0;
}