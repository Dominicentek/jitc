int main() {
    if (sizeof(0) != 4) return 1;
    if (sizeof(0U) != 4) return 2;
    if (sizeof(0L) != 8) return 3;
    if (sizeof(0LL) != 8) return 4;
    if (sizeof(0ULL) != 8) return 5;
}