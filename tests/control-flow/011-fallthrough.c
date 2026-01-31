int main() {
    int x = 0;
    switch (3) {
        case 1: x |= 1 << 0;
        case 2: x |= 1 << 1;
        case 3: x |= 1 << 2;
        case 4: x |= 1 << 3; break;
        case 5: x |= 1 << 4;
    }
    return x - 0b01100;
}