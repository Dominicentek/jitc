int main() {
    float f = 1.0f;
    double d = 1.0;
    int i = 1;
    if ((float)d != 1.0f) return 1;
    if ((double)f != 1.0) return 2;
    if ((float)d != (double)f) return 3;
    if ((int)f != 1) return 4;
    if ((int)d != 1) return 5;
    if ((float)i != 1.0f) return 6;
    if ((double)i != 1.0) return 7;
    return 0;
}