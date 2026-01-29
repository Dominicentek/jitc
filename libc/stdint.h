#ifndef __STD_STDINT_H
#define __STD_STDINT_H

#include "stddef.h"

typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

typedef uint64_t uintmax_t;
typedef int64_t intmax_t;

typedef uintmax_t uintptr_t;
typedef intmax_t intptr_t;

#define INT8_MIN   (int8_t)(0xFFUL)
#define INT16_MIN  (int16_t)(0xFFFFUL)
#define INT32_MIN  (int32_t)(0xFFFFFFFFUL)
#define INT64_MIN  (int64_t)(0xFFFFFFFFFFFFFFFFUL)

#define INT8_MAX   (int8_t)(0x7FUL)
#define INT16_MAX  (int16_t)(0x7FFFUL)
#define INT32_MAX  (int32_t)(0x7FFFFFFFUL)
#define INT64_MAX  (int64_t)(0x7FFFFFFFFFFFFFFFUL)

#define UINT8_MAX   (uint8_t)(0xFFUL)
#define UINT16_MAX  (uint16_t)(0xFFFFUL)
#define UINT32_MAX  (uint32_t)(0xFFFFFFFFUL)
#define UINT64_MAX  (uint64_t)(0xFFFFFFFFFFFFFFFFUL)

#define INTMAX_MIN INT64_MIN
#define INTMAX_MAX INT64_MAX
#define UINTMAX_MAX UINT64_MAX

#endif
