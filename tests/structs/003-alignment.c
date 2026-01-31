typedef struct {
    char x;
    long y;
    char z;
} size24;

typedef struct {
    char x; 
    char y;
    long z;
} size16;

int main() {
    if (sizeof(size16) != 16) return 1;
    if (sizeof(size24) != 24) return 2;
    return 0;
}