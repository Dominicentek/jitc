struct Test {
    int hello;
    struct {
        int hi;
    } hi;
};

int main() {
    struct Test* test = 0;
    test->hello = 0;
    test->hi.hi = 3;
}
