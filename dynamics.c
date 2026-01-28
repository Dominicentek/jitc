#include "dynamics.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include "compares.h"

typedef struct {
    void* key;
    void* value;
} kvpair_t;

struct string_t {
    char* data;
    size_t length, capacity;
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

void str_append(string_t* str, const char* other) {
    int len = strlen(other);
    int prev_cap = str->capacity;
    while (str->length + len + 1 >= str->capacity) str->capacity += 64;
    if (str->capacity != prev_cap) str->data = realloc(str->data, str->capacity);
    strcpy(str->data + str->length, other);
    str->length += len;
    str->data[str->length] = 0;
}

void str_appendf(string_t* str, const char* fmt, ...) {
    va_list args1, args2;
    va_start(args1, fmt);
    va_copy(args2, args1);
    char data[vsnprintf(NULL, 0, fmt, args1) + 1];
    vsprintf(data, fmt, args2);
    va_end(args1);
    va_end(args2);
    str_append(str, data);
}

void str_delete(string_t* str) {
    free(str->data);
    free(str);
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
        writer->data = realloc(writer->data, writer->capacity);
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

list_t* __list_new(size_t item_size) {
    __list_t* list = malloc(sizeof(__list_t));
    list->capacity = 4;
    list->length = 0;
    list->item_size = item_size;
    list->list = malloc(item_size * list->capacity);
    return list;
}

size_t list_size(list_t* list) {
    return ((__list_t*)list)->length;
}

void* __list_add(list_t* _list) {
    __list_t* list = _list;
    if (list->length == list->capacity) {
        list->capacity *= 2;
        list->list = realloc(list->list, list->item_size * list->capacity);
    }
    return &list->list[list->length++ * list->item_size];
}

void* __list_get(list_t* _list, size_t index) {
    __list_t* list = _list;
    if (index >= list->length) return NULL;
    return &list->list[index * list->item_size];
}

void list_remove(list_t* _list, size_t index) {
    __list_t* list = _list;
    if (index >= list->length) return;
    list->length--;
    if (index != list->length) memmove(
        list->list + index * list->item_size,
        list->list + (index + 1) * list->item_size,
        list->item_size * (list->length - index)
    );
}

void list_delete(list_t* _list) {
    __list_t* list = _list;
    free(list->list);
    free(list);
}

map_t* __map_new(compare_t compare, size_t key_size, size_t value_size) {
    __map_t* map = malloc(sizeof(__map_t));
    map->compare = compare;
    map->capacity = 4;
    map->length = 0;
    map->cursor = NULL;
    map->key_size = key_size;
    map->pair_size = key_size + value_size;
    map->entries = malloc(map->pair_size * map->capacity);
    return map;
}

size_t map_size(map_t* map) {
    return ((__map_t*)map)->length;
}

bool map_find(map_t* _map, void* key) {
    __map_t* map = _map;
    return map->cursor = bsearch(key, map->entries, map->length, map->pair_size, map->compare);
}

void map_index(map_t* _map, size_t index) {
    __map_t* map = _map;
    if (index >= map->length) return;
    map->cursor = &map->entries[index * map->pair_size];
}

#include <stdio.h>

bool map_commit(map_t* _map) {
    __map_t* map = _map;
    void* last_entry = &map->entries[(map->length - 1) * map->pair_size];
    if ((map->cursor = bsearch(last_entry, map->entries, map->length - 1, map->pair_size, map->compare))) {
        map->length--;
        return false;
    }
    char copy[map->key_size];
    memcpy(copy, last_entry, map->key_size);
    qsort(map->entries, map->length, map->pair_size, map->compare);
    map->cursor = bsearch(copy, map->entries, map->length, map->pair_size, map->compare);
    return true;
}

void* __map_add(map_t* _map) {
    __map_t* map = _map;
    if (map->length == map->capacity) {
        map->capacity *= 2;
        map->entries = realloc(map->entries, map->pair_size * map->capacity);
    }
    return map->cursor = &map->entries[map->length++ * map->pair_size];
}

void* __map_get_key(map_t* _map) {
    __map_t* map = _map;
    if (!map->cursor) return NULL;
    return map->cursor;
}

void* __map_get_value(map_t* _map) {
    __map_t* map = _map;
    if (!map->cursor) return NULL;
    return map->cursor + map->key_size;
}

void map_remove(map_t* _map) {
    __map_t* map = _map;
    if (!map->cursor) return;
    size_t index = ((uintptr_t)map->cursor - (uintptr_t)map->entries) / map->pair_size;
    map->length--;
    if (index != map->length) memmove(
        map->entries + index * map->pair_size,
        map->entries + (index + 1) * map->pair_size,
        map->pair_size * (map->length - index)
    );
    map->cursor = NULL;
}

void map_delete(map_t* _map) {
    __map_t* map = _map;
    free(map->entries);
    free(map);
}

set_t* __set_new(compare_t compare, size_t item_size) {
    __set_t* set = malloc(sizeof(__set_t));
    set->compare = compare;
    set->capacity = 4;
    set->length = 0;
    set->item_size = item_size;
    set->list = malloc(set->item_size * set->capacity);
    return set;
}

size_t set_size(set_t* set) {
    return ((__set_t*)set)->length;
}

int set_indexof(set_t* _set, void* key) {
    __set_t* set = _set;
    void* ptr = bsearch(key, set->list, set->length, set->item_size, set->compare);
    if (!ptr) return -1;
    return ((uintptr_t)ptr - (uintptr_t)set->list) / set->item_size;
}

void* __set_add(set_t* _set) {
    __set_t* set = _set;
    if (set->length == set->capacity) {
        set->capacity *= 2;
        set->list = realloc(set->list, set->item_size * set->capacity);
    }
    return &set->list[set->length++ * set->item_size];
}

void* __set_get(set_t* _set, size_t index) {
    __set_t* set = _set;
    if (index >= set->length) return NULL;
    return &set->list[index * set->item_size];
}

void set_remove(set_t* _set, size_t index) {
    __set_t* set = _set;
    if (index >= set->length) return;
    set->length--;
    if (index != set->length) memmove(
        set->list + index * set->item_size,
        set->list + (index + 1) * set->item_size,
        set->item_size * (set->length - index)
    );
}

bool set_commit(set_t* _set) {
    __set_t* set = _set;
    if (bsearch(&set->list[(set->length - 1) * set->item_size], set->list, set->length - 1, set->item_size, set->compare)) return false;
    qsort(set->list, set->length, set->item_size, set->compare);
    return true;
}

void set_delete(set_t* _set) {
    __set_t* set = _set;
    free(set->list);
    free(set);
}

stack_t* __stack_new(size_t item_size) {
    __stack_t* stack = malloc(sizeof(__stack_t));
    stack->capacity = 4;
    stack->length = 0;
    stack->item_size = item_size;
    stack->list = malloc(stack->capacity * stack->item_size);
    return stack;
}

size_t stack_size(stack_t* stack) {
    return ((__stack_t*)stack)->length;
}

void* __stack_push(stack_t* _stack) {
    __stack_t* stack = _stack;
    if (stack->length == stack->capacity) {
        stack->capacity *= 2;
        stack->list = realloc(stack->list, stack->item_size * stack->capacity);
    }
    return &stack->list[stack->length++ * stack->item_size];
}

void* __stack_peek(stack_t* _stack) {
    __stack_t* stack = _stack;
    if (stack->length == 0) return NULL;
    return &stack->list[(stack->length - 1) * stack->item_size];
}

void* __stack_pop(stack_t* _stack) {
    __stack_t* stack = _stack;
    if (stack->length == 0) return NULL;
    return &stack->list[--stack->length * stack->item_size];
}

void stack_delete(stack_t* _stack) {
    __stack_t* stack = _stack;
    free(stack->list);
    free(stack);
}

queue_t* __queue_new(size_t item_size) {
    __queue_t* queue = malloc(sizeof(__queue_t));
    queue->capacity = 4;
    queue->length = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->item_size = item_size;
    queue->list = malloc(queue->capacity * queue->item_size);
    return queue;
}

size_t queue_size(queue_t* queue) {
    return ((__queue_t*)queue)->length;
}

void* __queue_push(queue_t* _queue) {
    __queue_t* queue = _queue;
    if (queue->length == queue->capacity) {
        queue->capacity *= 2;
        void* new_items = malloc(queue->item_size * queue->capacity);
        memcpy(new_items, queue->list + queue->tail, queue->item_size * (queue->length - queue->tail));
        memcpy(new_items + queue->length - queue->tail, queue->list, queue->item_size * queue->tail);
        free(queue->list);
        queue->tail = 0;
        queue->head = queue->length;
        queue->list = new_items;
    }
    queue->length++;
    void* ptr = &queue->list[queue->head * queue->item_size];
    queue->head = (queue->head + 1) % queue->capacity;
    return ptr;
}

void* __queue_peek(queue_t* _queue) {
    __queue_t* queue = _queue;
    if (queue->length == 0) return NULL;
    return &queue->list[queue->tail * queue->item_size];
}

void* __queue_pop(queue_t* _queue) {
    __queue_t* queue = _queue;
    if (queue->length == 0) return NULL;
    queue->length--;
    void* ptr = &queue->list[queue->tail * queue->item_size];
    queue->tail = (queue->tail + 1) % queue->capacity;
    return ptr;
}

void* __queue_rollback(queue_t* _queue) {
    __queue_t* queue = _queue;
    if (queue->length == 0) return NULL;
    queue->length++;
    return &queue->list[--queue->tail * queue->item_size];
}

void queue_delete(queue_t* _queue) {
    __queue_t* queue = _queue;
    free(queue->list);
    free(queue);
}
