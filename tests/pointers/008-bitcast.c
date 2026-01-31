int main() {
    float f = 1.5f;
    int i = *(int*)&f;
    if (i == 0x3fc00000) return 0;
    return 1;
}