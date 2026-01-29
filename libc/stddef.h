#ifndef __STD_STDDEF_H
#define __STD_STDDEF_H

#define NULL nullptr
#define offsetof(type, member) ((size_t)((char*)&((typeof(type)*)0)->member - (char*)0))

typedef unsigned long size_t;
typedef long ptrdiff_t;
typedef long ssize_t;
typedef long time_t;
typedef long clock_t;

#endif
