int main() {
    int result = 0;
    0 && (result = 1);
    1 || (result = 2);
    return result;
}