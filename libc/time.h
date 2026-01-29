#ifndef __STD_TIME_H
#define __STD_TIME_H

#include "stddef.h"

struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
	long __tm_gmtoff;
	const char* __tm_zone;
};

clock_t clock(void);
time_t time(time_t*);
double difftime(time_t, time_t);
time_t mktime(struct tm*);
size_t strftime(char*, size_t, const char*, const struct tm*);
struct tm* gmtime (const time_t*);
struct tm* localtime (const time_t*);
char* asctime(const struct tm*);
char* ctime(const time_t*);
int timespec_get(struct timespec*, int);

#define CLOCKS_PER_SEC 1000000L

#endif
