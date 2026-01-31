typedef union {
    int i;
    float f;
} int_float;

int main() {
    int_float u;
    u.f = 1.5f;
    return u.i != 0x3fc00000;
}