#ifndef JITC_DYNAMICS_H
#define JITC_DYNAMICS_H

#include <stddef.h>
#include <stdint.h>

typedef int(*compare_t)(const void* a, const void* b);

typedef struct string_t string_t;
typedef struct list_t list_t;
typedef struct map_t map_t;
typedef struct set_t set_t;
typedef struct stack_t stack_t;
typedef struct queue_t queue_t;
typedef struct structwriter_t structwriter_t;
typedef struct bytewriter_t bytewriter_t;
typedef struct bytereader_t bytereader_t;

string_t* str_new();
char* str_data(string_t* str);
size_t str_length(string_t* str);
void str_clear(string_t* str);
void str_append(string_t* str, char* other);
void str_delete(string_t* str);

list_t* list_new();
size_t list_size(list_t* list);
void list_add_int(list_t* list, uint64_t item);
void list_add_float(list_t* list, double item);
void list_add_ptr(list_t* list, void* item);
uint64_t list_get_int(list_t* list, size_t index);
double list_get_float(list_t* list, size_t index);
void* list_get_ptr(list_t* list, size_t index);
void* list_get(list_t* list, size_t index);
size_t list_indexof_int(list_t* list, uint64_t item);
size_t list_indexof_float(list_t* list, double item);
size_t list_indexof_ptr(list_t* list, void* item);
void list_remove_int(list_t* list, uint64_t item);
void list_remove_float(list_t* list, double item);
void list_remove_ptr(list_t* list, void* item);
void list_remove(list_t* list, size_t index);
void list_delete(list_t* list);

map_t* map_new(compare_t compare);
size_t map_size(map_t* map);
void map_store_int(map_t* map, uint64_t value);
void map_store_float(map_t* map, double value);
void map_store_ptr(map_t* map, void* value);
uint64_t map_as_int(map_t* map);
double map_as_float(map_t* map);
void* map_as_ptr(map_t* map);
void* map_get_int(map_t* map, uint64_t key);
void* map_get_float(map_t* map, double key);
void* map_get_ptr(map_t* map, void* key);
void* map_find_int(map_t* map, uint64_t key);
void* map_find_float(map_t* map, double key);
void* map_find_ptr(map_t* map, void* key);
void* map_index(map_t* map, size_t index);
uint64_t map_get_int_key(map_t* map, size_t index);
double map_get_float_key(map_t* map, size_t index);
void* map_get_ptr_key(map_t* map, size_t index);
void map_delete(map_t* map);

set_t* set_new(compare_t compare);
size_t set_size(set_t* set);
void set_add_int(set_t* set, uint64_t item);
void set_add_float(set_t* set, double item);
void set_add_ptr(set_t* set, void* item);
uint64_t set_get_int(set_t* set, size_t index);
double set_get_float(set_t* set, size_t index);
void* set_get_ptr(set_t* set, size_t index);
void* set_get(set_t* set, size_t index);
void set_remove_int(set_t* set, uint64_t item);
void set_remove_float(set_t* set, double item);
void set_remove_ptr(set_t* set, void* item);
void set_remove(set_t* set, size_t index);
uint64_t* set_find_int(set_t* set, uint64_t item);
double* set_find_float(set_t* set, double item);
void** set_find_ptr(set_t* set, void* item);
void set_delete(set_t* set);

stack_t* stack_new();
size_t stack_size(stack_t* stack);
void stack_push_int(stack_t* stack, uint64_t item);
void stack_push_float(stack_t* stack, double item);
void stack_push_ptr(stack_t* stack, void* item);
uint64_t stack_pop_int(stack_t* stack);
double stack_pop_float(stack_t* stack);
void* stack_pop_ptr(stack_t* stack);
void stack_pop(stack_t* stack);
uint64_t stack_peek_int(stack_t* stack);
double stack_peek_float(stack_t* stack);
void* stack_peek_ptr(stack_t* stack);
void stack_delete(stack_t* stack);

queue_t* queue_new();
size_t queue_size(queue_t* queue);
void queue_push_int(queue_t* queue, uint64_t item);
void queue_push_float(queue_t* queue, double item);
void queue_push_ptr(queue_t* queue, void* item);
uint64_t queue_pop_int(queue_t* queue);
double queue_pop_float(queue_t* queue);
void* queue_pop_ptr(queue_t* queue);
void queue_pop(queue_t* queue);
uint64_t queue_peek_int(queue_t* queue);
double queue_peek_float(queue_t* queue);
void* queue_peek_ptr(queue_t* queue);
void queue_delete(queue_t* queue);

structwriter_t* structwriter_new(size_t size);
void structwriter_int8(structwriter_t* writer, uint8_t value);
void structwriter_int16(structwriter_t* writer, uint16_t value);
void structwriter_int32(structwriter_t* writer, uint32_t value);
void structwriter_int64(structwriter_t* writer, uint64_t value);
void structwriter_float32(structwriter_t* writer, float value);
void structwriter_float64(structwriter_t* writer, double value);
void structwriter_pointer(structwriter_t* writer, void* value);
void structwriter_align(structwriter_t* writer, size_t alignment);
void* structwriter_delete(structwriter_t* writer);

bytewriter_t* bytewriter_new();
size_t bytewriter_size(bytewriter_t* writer);
void bytewriter_int8(bytewriter_t* writer, uint8_t value);
void bytewriter_int16(bytewriter_t* writer, uint16_t value);
void bytewriter_int32(bytewriter_t* writer, uint32_t value);
void bytewriter_int64(bytewriter_t* writer, uint64_t value);
void bytewriter_float32(bytewriter_t* writer, float value);
void bytewriter_float64(bytewriter_t* writer, double value);
void bytewriter_pointer(bytewriter_t* writer, void* value);
void* bytewriter_delete(bytewriter_t* writer);

bytereader_t* bytereader_new(void* ptr);
uint8_t bytereader_int8(bytereader_t* reader);
uint16_t bytereader_int16(bytereader_t* reader);
uint32_t bytereader_int32(bytereader_t* reader);
uint64_t bytereader_int64(bytereader_t* reader);
float bytereader_float32(bytereader_t* reader);
double bytereader_float64(bytereader_t* reader);
void* bytereader_pointer(bytereader_t* reader);
void bytereader_delete(bytereader_t* reader);

#endif
