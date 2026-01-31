#include "stdlib.h"

typedef struct List List;
struct<T> List {
    List<T>* next;
    T item;
};

<T> void init(T item) {
    List<T>* list = malloc(sizeof(List<T>));
    list.next = nullptr;
    list.item = item;
    return list;
}

<T> void add(Array<T>* this, T item) {
    while (this.next) this = this.next;
    this.next = init(item);
}

int main() {
    Array<int> arr;
    arr.init<int>();
    for (int i = 1; i <= 10; i++)
        arr.add<int>(i);
    
    int counter = 0;
    while (arr) {
        if (arr.item != ++counter) return counter;
        arr = arr.next;
    } 
    
    return 0;
}