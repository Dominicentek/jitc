#include "stdarg.h"

int sum(int count, ...) {
    int sum = 0;

    va_list list;
    va_start(list, count);
    for (int i = 0; i < count; i++)
        sum += va_arg(list, int);
    va_end(list);
    
    return sum;
}