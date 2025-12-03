struct test {
    int bar;
};

struct test foo() {}

int main() {
    foo().bar;
}
