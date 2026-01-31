int main() {
    float x = 2.5f;
    float y = 0.5f;
    if (x + y != 3.0f) return 1;
    if (x - y != 2.0f) return 2;
    if (x * y != 1.25f) return 3;
    if (x / y != 5.0f) return 4;
    if (-y != -0.5f) return 5;
    return 0;
}