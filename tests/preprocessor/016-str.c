int main() {
    if (__STR__(test)[1] != 'e') return 1;
    if (__STR__("test")[2] != 's') return 2;
    if (*__STR__(,) != ',') return 2;

    return 0;
}
