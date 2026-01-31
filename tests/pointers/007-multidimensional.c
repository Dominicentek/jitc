int main() {
    int arr[4][4];
    for (int x = 0; x < 4; x++)
        for (int y = 0; y < 4; y++)
            arr[y][x] = y * 4 + x;
    if (arr[3][2] != 14) return 1;
    int* row = arr[3];
    if (row[2] != 14) return 2;
    return 0;
}