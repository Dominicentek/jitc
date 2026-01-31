typedef enum: long {
    Zero
} e;

int main() {
    if (sizeof(e) != 8) return 1;
    return Zero;
}