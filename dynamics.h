#ifndef JITC_DYNAMICS_H
#define JITC_DYNAMICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef int(*compare_t)(const void* a, const void* b);

#define __RETURNS(str, field, func, ...) (*(typeof((str)->field))func(str __VA_OPT__(,) __VA_ARGS__))

#define __PARAM(x, name) typeof(x)* name;
#define __DEFINE(type, fields) struct { fields char _[sizeof(__##type##_t) - sizeof(struct { fields })]; }

typedef struct string_t string_t;
typedef struct bytewriter_t bytewriter_t;

typedef void list_t;
typedef void map_t;
typedef void set_t;
typedef void stack_t;
typedef void queue_t;

typedef struct {
    uint8_t* list;
    size_t item_size;
    size_t length, capacity;
} __list_t;

typedef struct {
    uint8_t* entries;
    uint8_t* cursor;
    size_t key_size, pair_size;
    size_t length, capacity;
    compare_t compare;
} __map_t;

typedef struct {
    uint8_t* list;
    size_t item_size;
    size_t length, capacity;
    compare_t compare;
} __set_t;

typedef struct {
    uint8_t* list;
    size_t item_size;
    size_t length, capacity;
} __stack_t;

typedef struct {
    uint8_t* list;
    size_t item_size;
    size_t length, capacity, head, tail;
} __queue_t;

string_t* str_new();
char* str_data(string_t* str);
size_t str_length(string_t* str);
void str_clear(string_t* str);
void str_append(string_t* str, const char* other);
void str_appendf(string_t* str, const char* fmt, ...);
void str_delete(string_t* str);

bytewriter_t* bytewriter_new();
size_t bytewriter_size(bytewriter_t* writer);
uint8_t* bytewriter_data(bytewriter_t* writer);
void bytewriter_int8(bytewriter_t* writer, uint8_t value);
void bytewriter_int16(bytewriter_t* writer, uint16_t value);
void bytewriter_int32(bytewriter_t* writer, uint32_t value);
void bytewriter_int64(bytewriter_t* writer, uint64_t value);
void bytewriter_float32(bytewriter_t* writer, float value);
void bytewriter_float64(bytewriter_t* writer, double value);
void bytewriter_pointer(bytewriter_t* writer, void* value);
void* bytewriter_delete(bytewriter_t* writer);

list_t* __list_new(size_t item_size);
size_t list_size(list_t* list);
void list_clear(list_t* list);
void* __list_add(list_t* list);
void* __list_get(list_t* list, size_t index);
void list_remove(list_t* list, size_t index);
void list_delete(list_t* list);

#define list(T) __DEFINE(list, __PARAM(T, _l))
#define list_new(T) __list_new(sizeof(T))
#define list_add(list) __RETURNS(list, _l, __list_add)
#define list_get(list, index) __RETURNS(list, _l, __list_get, index)

map_t* __map_new(compare_t compare, size_t key_size, size_t value_size);
size_t map_size(map_t* map);
void map_clear(map_t* map);
bool map_find(map_t* map, void* key);
void map_index(map_t* map, size_t index);
bool map_commit(map_t* map);
void* __map_add(map_t* map);
void* __map_get_key(map_t* map);
void* __map_get_value(map_t* map);
void map_remove(map_t* map);
void map_delete(map_t* map);

#define map(K, V) __DEFINE(map, __PARAM(K, _k) __PARAM(V, _v))
#define map_new(compare, K, V) __map_new(compare, sizeof(K), sizeof(V))
#define map_add(map) __RETURNS(map, _k, __map_add)
#define map_get_key(map) __RETURNS(map, _k, __map_get_key)
#define map_get_value(map) __RETURNS(map, _v, __map_get_value)

set_t* __set_new(compare_t compare, size_t item_size);
size_t set_size(set_t* set);
void set_clear(set_t* set);
bool set_commit(set_t* set);
int set_indexof(set_t* set, void* key);
void* __set_add(set_t* set);
void* __set_get(set_t* set, size_t index);
void set_remove(set_t* set, size_t index);
void set_delete(set_t* set);

#define set(T) __DEFINE(set, __PARAM(T, _st))
#define set_new(compare, T) __set_new(compare, sizeof(T))
#define set_find(set, key) ({ int index = set_indexof(set, key); index == -1 ? NULL : &set_get(set, index); })
#define set_add(set) __RETURNS(set, _st, __set_add)
#define set_get(set, index) __RETURNS(set, _st, __set_get, index)

stack_t* __stack_new(size_t item_size);
size_t stack_size(stack_t* stack);
void stack_clear(stack_t* stack);
void* __stack_push(stack_t* stack);
void* __stack_peek(stack_t* stack);
void* __stack_pop(stack_t* stack);
void stack_delete(stack_t* stack);

#define stack(T) __DEFINE(stack, __PARAM(T, _sk))
#define stack_new(T) __stack_new(sizeof(T))
#define stack_push(stack) __RETURNS(stack, _sk, __stack_push)
#define stack_peek(stack) __RETURNS(stack, _sk, __stack_peek)
#define stack_pop(stack) __RETURNS(stack, _sk, __stack_pop)

queue_t* __queue_new(size_t item_size);
size_t queue_size(queue_t* queue);
void queue_clear(queue_t* queue);
void* __queue_push(queue_t* queue);
void* __queue_peek(queue_t* queue);
void* __queue_pop(queue_t* queue);
void* __queue_rollback(queue_t* queue);
void queue_delete(queue_t* queue);

#define queue(T) __DEFINE(queue, __PARAM(T, _q))
#define queue_new(T) __queue_new(sizeof(T))
#define queue_push(queue) __RETURNS(queue, _q, __queue_push)
#define queue_peek(queue) __RETURNS(queue, _q, __queue_peek)
#define queue_pop(queue) __RETURNS(queue, _q, __queue_pop)
#define queue_rollback(queue) __RETURNS(queue, _q, __queue_rollback)

#endif
