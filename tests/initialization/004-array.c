int main() {
    int x[4] = {1, 2, 3, 4, 5, 6};
    int y[4] = {1, 2, 3};
    
    if (x[3] != 4) return 1;
    if (y[3] != 0) return 2;
    return 0;
}