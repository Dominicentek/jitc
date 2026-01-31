#ifndef __STD_STDLIB_H
#define __STD_STDLIB_H

#include "stddef.h"

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

#define RAND_MAX (0x7FFFFFFF)

int atoi(const char*);
long atol(const char*);
long long atoll(const char*);
double atof(const char*);

float strtof(const char*, char**);
double strtod(const char*, char**);
long double strtold(const char*, char**);

long strtol(const char*, char**, int);
unsigned long strtoul(const char*, char**, int);
long long strtoll(const char*, char**, int);
unsigned long long strtoull(const char*, char**, int);

int rand(void);
void srand(unsigned);

void* malloc(size_t);
void* calloc(size_t, size_t);
void* realloc(void*, size_t);
void free(void *);

void abort(void);
void exit(int);

char* getenv(const char*);

int system(const char*);

void* bsearch(const void*, const void*, size_t, size_t, int(*)(const void*, const void*));
void qsort(void*, size_t, size_t, int(*)(const void*, const void*));

int abs(int);
long labs(long);
long long llabs(long long);

typedef struct { int quot, rem; } div_t;
typedef struct { long quot, rem; } ldiv_t;
typedef ldiv_t lldiv_t;

div_t div(int, int);
ldiv_t ldiv(long, long);
lldiv_t lldiv(long long, long long);

#endif
