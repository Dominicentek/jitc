struct test {
    long y, z;
    double x;
};

void func(struct test test) {}

int main() {
    struct test tset;
    func(tset);
}

