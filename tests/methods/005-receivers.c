int inc(int* this) -> (*this)++;

int main() {
    int x = 0;
    int y[1] = {0};
    struct { int x; } z = {0};
    long w = 0;
    
    x.inc();
    y[0].inc();
    z.x.inc();
    (*(int*)&w).inc();
    
    if (x != 1) return 1;
    if (y[0] != 1) return 2;
    if (z.x != 1) return 3;
    if (w != 1) return 4;
    
    return 0;
}