#include "stdlib.h"

int main() {
    int arr[] = {3, 1, 4, 2};
    qsort(arr, 4, sizeof(int), lambda(const void* a, const void* b): int -> *(int*)a - *(int*)b);
    if (arr[0] != 1) return 1;
    if (arr[1] != 2) return 2;
    if (arr[2] != 3) return 3;
    if (arr[3] != 4) return 4;
    return 0;
}