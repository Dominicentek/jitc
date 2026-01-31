typedef int(*func1)();
typedef func1(*func2)();
typedef func2(*func3)();
typedef func3(*func4)();
typedef func4(*func5)();
typedef func5(*func6)();
typedef func6(*func7)();
typedef func7(*func8)();

int get1() {
    return 0;
}

func1 get2() {
    return get1;
}

func2 get3() {
    return get2;
}

func3 get4() {
    return get3;
}

func4 get5() {
    return get4;
}

func5 get6() {
    return get5;
}

func6 get7() {
    return get6;
}

func7 get8() {
    return get7;
}

int main() {
    return get8()()()()()()()();
}