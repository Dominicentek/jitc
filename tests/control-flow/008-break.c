int main() {
    int x = 0;
    for (int i = 0; i < 10; i++) {
        if (x == 8) break;
        x++;
    }
    return x - 8;
}