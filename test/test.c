int main() {
    short x = 4;
    x += 3;
    x /= x & (x - 1);
    float y = 3;
    y -= 8.3 * y / (x + 1.5);
    x = (double)y;
    return ++x;
}