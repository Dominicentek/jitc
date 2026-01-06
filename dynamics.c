#include "dynamics.h"
#include <stdlib.h>
#include <string.h>

#include "compares.h"

typedef struct {
    void* key;
    void* value;
} kvpair_t;

struct string_t {
    char* data;
    size_t length, capacity;
};

struct set_t {
    void** list;
    compare_t compare;
    size_t length, capacity;
};

struct list_t {
    void** list;
    size_t length, capacity;
};

struct map_t {
    kvpair_t* entries;
    compare_t compare;
    size_t length, capacity;
    void* cursor;
};

struct stack_t {
    void** list;
    size_t length, capacity;
};

struct queue_t {
    void** list;
    size_t length, capacity, head, tail;
};

struct bytewriter_t {
    uint8_t* data;
    size_t size, capacity;
};

string_t* str_new() {
    string_t* str = malloc(sizeof(string_t));
    str->capacity = 64;
    str->length = 0;
    str->data = malloc(64);
    str->data[0] = 0;
    return str;
}

char* str_data(string_t* str) {
    return str->data;
}

size_t str_length(string_t* str) {
    return str->length;
}

void str_clear(string_t* str) {
    str->length = 0;
    str->data[0] = 0;
}

void str_append(string_t* str, char* other) {
    int len = strlen(other);
    int prev_cap = str->capacity;
    while (str->length + len + 1 >= str->capacity) str->capacity += 64;
    if (str->capacity != prev_cap) str->data = realloc(str->data, str->capacity);
    strcpy(str->data + str->length, other);
    str->length += len;
    str->data[str->length] = 0;
}

void str_delete(string_t* str) {
    free(str->data);
    free(str);
}

list_t* list_new() {
    list_t* list = malloc(sizeof(list_t));
    list->capacity = 4;
    list->length = 0;
    list->list = malloc(sizeof(void*) * list->capacity);
    return list;
}

size_t list_size(list_t* list) {
    return list->length;
}

void list_add_int(list_t* list, uint64_t item) {
    list_add_ptr(list, *(void**)&item);
}

void list_add_float(list_t* list, double item) {
    list_add_ptr(list, *(void**)&item);
}

void list_add_ptr(list_t* list, void* ptr) {
    if (list->length == list->capacity) {
        list->capacity *= 2;
        list->list = realloc(list->list, sizeof(void*) * list->capacity);
    }
    list->list[list->length++] = ptr;
}

uint64_t list_get_int(list_t* list, size_t index) {
    if (index >= list->length) return 0;
    return *(uint64_t*)&list->list[index];
}

double list_get_float(list_t* list, size_t index) {
    if (index >= list->length) return 0;
    return *(double*)&list->list[index];
}

void* list_get_ptr(list_t* list, size_t index) {
    if (index >= list->length) return NULL;
    return list->list[index];
}

void* list_get(list_t* list, size_t index) {
    if (index >= list->length) return NULL;
    return &list->list[index];
}

size_t list_indexof_int(list_t* list, uint64_t item) {
    return list_indexof_ptr(list, *(void**)&item);
}

size_t list_indexof_float(list_t* list, double item) {
    return list_indexof_ptr(list, *(void**)&item);
}

size_t list_indexof_ptr(list_t* list, void* item) {
    for (size_t i = 0; i < list->length; i++) {
        if (list->list[i] == item) return i;
    }
    return -1;
}

void list_remove_int(list_t* list, uint64_t item) {
    list_remove_ptr(list, *(void**)&item);
}

void list_remove_float(list_t* list, double item) {
    list_remove_ptr(list, *(void**)&item);
}

void list_remove_ptr(list_t* list, void* ptr) {
    size_t index = list_indexof_ptr(list, ptr);
    if (index == -1) return;
    list_remove(list, index);
}

void list_remove(list_t* list, size_t index) {
    if (index >= list->length) return;
    list->length--;
    if (index != list->length) memmove(
        list->list + index, list->list + index + 1,
        sizeof(void*) * (list->length - index)
    );
}

void list_delete(list_t* list) {
    free(list->list);
    free(list);
}

map_t* map_new(compare_t compare) {
    map_t* map = malloc(sizeof(map_t));
    map->compare = compare;
    map->capacity = 4;
    map->length = 0;
    map->cursor = NULL;
    map->entries = malloc(sizeof(map_t) * map->capacity);
    return map;
}

size_t map_size(map_t* map) {
    return map->length;
}

void map_store_int(map_t* map, uint64_t value) {
    if (!map->cursor) return;
    *(uint64_t*)map->cursor = value;
}

void map_store_float(map_t* map, double value) {
    if (!map->cursor) return;
    *(double*)map->cursor = value;
}

void map_store_ptr(map_t* map, void* value) {
    if (!map->cursor) return;
    *(void**)map->cursor = value;
}

uint64_t map_as_int(map_t* map) {
    if (!map->cursor) return 0;
    return *(uint64_t*)map->cursor;
}

double map_as_float(map_t* map) {
    if (!map->cursor) return 0;
    return *(double*)map->cursor;
}

void* map_as_ptr(map_t* map) {
    if (!map->cursor) return NULL;
    return *(void**)map->cursor;
}

void* map_get_int(map_t* map, uint64_t value) {
    return map_get_ptr(map, *(void**)&value);
}

void* map_get_float(map_t* map, double value) {
    return map_get_ptr(map, *(void**)&value);
}

void* map_get_ptr(map_t* map, void* value) {
    kvpair_t* ptr = bsearch(&value, map->entries, map->length, sizeof(kvpair_t), map->compare);
    if (!ptr) {
        if (map->length == map->capacity) {
            map->capacity *= 2;
            map->entries = realloc(map->entries, sizeof(kvpair_t) * map->capacity);
        }
        kvpair_t* entry = &map->entries[map->length++];
        entry->key = value;
        entry->value = NULL;
        qsort(map->entries, map->length, sizeof(kvpair_t), map->compare);
        ptr = bsearch(&value, map->entries, map->length, sizeof(kvpair_t), map->compare);
    }
    return map->cursor = &ptr->value;
}

void* map_find_int(map_t* map, uint64_t key) {
    return map_find_ptr(map, *(void**)&key);
}

void* map_find_float(map_t* map, double key) {
    return map_find_ptr(map, *(void**)&key);
}

void* map_find_ptr(map_t* map, void* key) {
    kvpair_t* ptr = bsearch(&key, map->entries, map->length, sizeof(kvpair_t), map->compare);
    return map->cursor = ptr ? &ptr->value : NULL;
}

void* map_index(map_t* map, size_t index) {
    return map->cursor = index < map->length ? &map->entries[index].value : NULL;
}

uint64_t map_get_int_key(map_t* map, size_t index) {
    if (index >= map->length) return 0;
    return *(uint64_t*)&map->entries[index].key;
}

double map_get_float_key(map_t* map, size_t index) {
    if (index >= map->length) return 0;
    return *(double*)&map->entries[index].key;
}

void* map_get_ptr_key(map_t* map, size_t index) {
    if (index >= map->length) return NULL;
    return *(void**)&map->entries[index].key;
}

void map_remove(map_t* map) {
    if (!map->cursor) return;
    kvpair_t* entry = (kvpair_t*)&((uint64_t*)map->cursor)[-1];
    size_t index = entry - map->entries;
    map->length--;
    if (index != map->length) memmove(
        map->entries + index, map->entries + index + 1,
        sizeof(kvpair_t) * (map->length - index)
    );
}

void map_delete(map_t* map) {
    free(map->entries);
    free(map);
}

set_t* set_new(compare_t compare) {
    set_t* set = malloc(sizeof(set_t));
    set->compare = compare;
    set->capacity = 4;
    set->length = 0;
    set->list = malloc(sizeof(void*) * set->capacity);
    return set;
}

size_t set_size(set_t* set) {
    return set->length;
}

void set_add_int(set_t* set, uint64_t item) {
    set_add_ptr(set, *(void**)&item);
}

void set_add_float(set_t* set, double item) {
    set_add_ptr(set, *(void**)&item);
}

void set_add_ptr(set_t* set, void* item) {
    if (set_find_ptr(set, item)) return;
    if (set->length == set->capacity) {
        set->capacity *= 2;
        set->list = realloc(set->list, sizeof(void*) * set->capacity);
    }
    set->list[set->length++] = item;
    qsort(set->list, set->length, sizeof(void*), set->compare);
}

uint64_t set_get_int(set_t* set, size_t index) {
    if (index >= set->length) return 0;
    return *(uint64_t*)&set->list[index];
}

double set_get_float(set_t* set, size_t index) {
    if (index >= set->length) return 0;
    return *(double*)&set->list[index];
}

void* set_get_ptr(set_t* set, size_t index) {
    if (index >= set->length) return NULL;
    return set->list[index];
}

void* set_get(set_t* set, size_t index) {
    return &set->list[index];
}

void set_remove_int(set_t* set, uint64_t item) {
    set_remove_ptr(set, *(void**)&item);
}

void set_remove_float(set_t* set, double item) {
    set_remove_ptr(set, *(void**)&item);
}

void set_remove_ptr(set_t* set, void* item) {
    void** ptr = set_find_ptr(set, item);
    if (!ptr) return;
    size_t index = ((uintptr_t)ptr - (uintptr_t)set->list) / sizeof(void*);
    set_remove(set, index);
}

void set_remove(set_t* set, size_t index) {
    if (index >= set->length) return;
    set->length--;
    if (index != set->length) memmove(
        set->list + index, set->list + index + 1,
        sizeof(void*) * (set->length - index)
    );
}

uint64_t* set_find_int(set_t* set, uint64_t item) {
    return (uint64_t*)set_find_ptr(set, *(void**)&item);
}

double* set_find_float(set_t* set, double item) {
    return (double*)set_find_ptr(set, *(double**)&item);
}

void** set_find_ptr(set_t* set, void* item) {
    return bsearch(&item, set->list, set->length, sizeof(void*), set->compare);
}

void set_delete(set_t* set) {
    free(set->list);
    free(set);
}

stack_t* stack_new() {
    stack_t* stack = malloc(sizeof(stack_t));
    stack->capacity = 4;
    stack->length = 0;
    stack->list = malloc(stack->capacity * sizeof(void*));
    return stack;
}

size_t stack_size(stack_t* stack) {
    return stack->length;
}

void stack_push_int(stack_t* stack, uint64_t item) {
    stack_push_ptr(stack, *(void**)&item);
}

void stack_push_float(stack_t* stack, double item) {
    stack_push_ptr(stack, *(void**)&item);
}

void stack_push_ptr(stack_t* stack, void* item) {
    if (stack->length == stack->capacity) {
        stack->capacity *= 2;
        stack->list = realloc(stack->list, sizeof(void*) * stack->capacity);
    }
    stack->list[stack->length++] = item;
}

uint64_t stack_pop_int(stack_t* stack) {
    if (stack->length == 0) return 0;
    stack_pop(stack);
    return *(uint64_t*)&stack->list[stack->length];
}

double stack_pop_float(stack_t* stack) {
    if (stack->length == 0) return 0;
    stack_pop(stack);
    return *(double*)stack->list[stack->length];
}

void* stack_pop_ptr(stack_t* stack) {
    if (stack->length == 0) return NULL;
    stack_pop(stack);
    return stack->list[stack->length];
}

void stack_pop(stack_t* stack) {
    if (stack->length == 0) return;
    stack->length--;
}

uint64_t stack_peek_int(stack_t* stack) {
    if (stack->length == 0) return 0;
    return *(uint64_t*)&stack->list[stack->length - 1];
}

double stack_peek_float(stack_t* stack) {
    if (stack->length == 0) return 0;
    return *(double*)stack->list[stack->length - 1];
}

void* stack_peek_ptr(stack_t* stack) {
    if (stack->length == 0) return NULL;
    return stack->list[stack->length - 1];
}

void stack_delete(stack_t* stack) {
    free(stack->list);
    free(stack);
}

queue_t* queue_new() {
    queue_t* queue = malloc(sizeof(queue_t));
    queue->capacity = 4;
    queue->length = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->list = malloc(queue->capacity * sizeof(void*));
    return queue;
}

size_t queue_size(queue_t* queue) {
    return queue->length;
}

void queue_push_int(queue_t* queue, uint64_t item) {
    queue_push_ptr(queue, *(void**)&item);
}

void queue_push_float(queue_t* queue, double item) {
    queue_push_ptr(queue, *(double**)&item);
}

void queue_push_ptr(queue_t* queue, void* item) {
    if (queue->length == queue->capacity) {
        queue->capacity *= 2;
        void* new_items = malloc(sizeof(void*) * queue->capacity);
        memcpy(new_items, queue->list + queue->tail, sizeof(void*) * (queue->length - queue->tail));
        memcpy(new_items + queue->length - queue->tail, queue->list, sizeof(void*) * queue->tail);
        free(queue->list);
        queue->tail = 0;
        queue->head = queue->length;
        queue->list = new_items;
    }
    queue->list[queue->head++] = item;
    queue->length++;
}

uint64_t queue_pop_int(queue_t* queue) {
    if (queue->length == 0) return 0;
    queue->length--;
    return *(uint64_t*)&queue->list[queue->tail++];
}

double queue_pop_float(queue_t* queue) {
    if (queue->length == 0) return 0;
    queue->length--;
    return *(double*)&queue->list[queue->tail++];
}

void* queue_pop_ptr(queue_t* queue) {
    if (queue->length == 0) return NULL;
    queue->length--;
    return queue->list[queue->tail++];
}

void queue_pop(queue_t* queue) {
    if (queue->length == 0) return;
    queue->length--;
    queue->tail++;
}

uint64_t queue_peek_int(queue_t* queue) {
    if (queue->length == 0) return 0;
    return *(uint64_t*)&queue->list[queue->tail];
}

double queue_peek_float(queue_t* queue) {
    if (queue->length == 0) return 0;
    return *(double*)&queue->list[queue->tail];
}

void* queue_peek_ptr(queue_t* queue) {
    if (queue->length == 0) return NULL;
    return queue->list[queue->tail];
}

void queue_delete(queue_t* queue) {
    free(queue->list);
    free(queue);
}

bytewriter_t* bytewriter_new() {
    bytewriter_t* writer = malloc(sizeof(bytewriter_t));
    writer->size = 0;
    writer->capacity = 64;
    writer->data = malloc(writer->capacity);
    return writer;
}

size_t bytewriter_size(bytewriter_t* writer) {
    return writer->size;
}

uint8_t* bytewriter_data(bytewriter_t* writer) {
    return writer->data;
}

static void bytewriter_write(bytewriter_t* writer, void* ptr, size_t size) {
    if (writer->size + size + 1 >= writer->capacity) {
        writer->capacity *= 2;
        writer->data = realloc(writer->data, sizeof(void*) * writer->capacity);
    }
    memcpy((uint8_t*)writer->data + writer->size, ptr, size);
    writer->size += size;
}

void bytewriter_int8(bytewriter_t* writer, uint8_t value) {
    bytewriter_write(writer, &value, sizeof(value));
}

void bytewriter_int16(bytewriter_t* writer, uint16_t value) {
    bytewriter_write(writer, &value, sizeof(value));
}

void bytewriter_int32(bytewriter_t* writer, uint32_t value) {
    bytewriter_write(writer, &value, sizeof(value));
}

void bytewriter_int64(bytewriter_t* writer, uint64_t value) {
    bytewriter_write(writer, &value, sizeof(value));
}

void bytewriter_float32(bytewriter_t* writer, float value) {
    bytewriter_write(writer, &value, sizeof(value));
}

void bytewriter_float64(bytewriter_t* writer, double value) {
    bytewriter_write(writer, &value, sizeof(value));
}

void bytewriter_pointer(bytewriter_t* writer, void* value) {
    bytewriter_write(writer, &value, sizeof(value));
}

void* bytewriter_delete(bytewriter_t* writer) {
    void* ptr = writer->data;
    void* copy = malloc(writer->size);
    memcpy(copy, writer->data, writer->size);
    free(writer->data);
    free(writer);
    return copy;
}
