#define long int

long x = 0;

#undef long

long y = 0;

int main() {
    if (sizeof(x) != 4) return 1;
    if (sizeof(y) != 8) return 2;
    return 0;
}