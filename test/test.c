struct test {
    int x[1024];
};

int main() {
    struct test test1;
    const struct test test2;
    test1 = test2;
}

