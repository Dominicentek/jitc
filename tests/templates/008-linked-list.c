#include "stdlib.h"

typedef struct<T> List {
    struct List<T>* next;
    T item;
} List;

<T> void init() {
    List<T>* list = malloc(sizeof(List<T>));
    list.next = nullptr;
    return list;
}

<T> void add(List<T>* this, T item) {
    while (this.next) this = this.next;
    this.item = item;
    this.next = init<T>();
}

int main() {
    List<int>* list = init<int>();
    for (int i = 1; i <= 10; i++)
        list.add<int>(i);

    int counter = 0;
    while (list) {
        if (list.item != ++counter) return counter;
        list = list.next;
    }

    return 0;
}
