int main() {
    int x = 10;
    start:
    x--;
    goto check;
    end:
    return x;
    check:
    if (x > 0) goto start;
    goto end;
}