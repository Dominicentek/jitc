int main() {
    int test = 0;
    for (int i = 0; i < 10; i++) {
        *&test = i;
    }
}
