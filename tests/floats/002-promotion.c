int main() {
    if (sizeof(0.0f + 0.0f) != 4) return 1;
    if (sizeof(0.0  + 0.0f) != 8) return 2;
    if (sizeof(0.0  + 0.0)  != 8) return 3;
    return 0;
}