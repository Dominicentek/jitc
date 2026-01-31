struct<T> s {
    T x;
};

int main() {
    struct s<int> s;
    s.x = 0;
    return s.x;
}