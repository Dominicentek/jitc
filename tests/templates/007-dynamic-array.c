#include "stdlib.h"

typedef struct<T> {
    int size, capacity;
    T* items;
} Array;

<T> void init(Array<T>* this) {
    this.size = 0;
    this.capacity = 4;
    this.items = malloc(sizeof(T) * this.capacity);
}

<T> void add(Array<T>* this, T item) {
    if (this.size == this.capacity) {
        this.capacity *= 2;
        this.items = realloc(this.items, sizeof(T) * this.capacity);
    }
    this.items[this.size++] = item;
}

int main() {
    Array<int> arr;
    arr.init<int>();
    for (int i = 1; i <= 10; i++)
        arr.add<int>(i);
    
    if (arr.items[0] != 1) return 1;
    if (arr.items[1] != 2) return 2;
    if (arr.items[2] != 3) return 3;
    if (arr.items[3] != 4) return 4;
    if (arr.items[4] != 5) return 5;
    if (arr.items[5] != 6) return 6;
    if (arr.items[6] != 7) return 7;
    if (arr.items[7] != 8) return 8;
    if (arr.items[8] != 9) return 9;
    if (arr.items[9] != 10) return 10;
    
    return 0;
}