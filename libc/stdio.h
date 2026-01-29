#ifndef __STD_STDIO_H
#define __STD_STDIO_H

#include "stddef.h"

typedef struct FILE FILE;

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define BUFSIZ 1024

#ifdef _WIN32
#define IO_BINARY "b"
#else
#define IO_BINARY
#endif

#define IO_RDONLY "r"
#define IO_WRONLY "w"
#define IO_APPEND "a"
#define IO_RDWR "r+"
#define IO_RDWR_CREATE "w+"
#define IO_RDWR_APPEND "a+"

extern FILE* const stdin;
extern FILE* const stdout;
extern FILE* const stderr;

FILE* fopen(const char*, const char*);
FILE* freopen(const char*, const char*, FILE*);
int fclose(FILE* );

int remove(const char *);
int rename(const char *, const char *);

int feof(FILE*);
int ferror(FILE*);
int fflush(FILE*);
void clearerr(FILE*);

int fseek(FILE*, long, int);
long ftell(FILE*);
void rewind(FILE*);

size_t fread(void*, size_t, size_t, FILE*);
size_t fwrite(const void*, size_t, size_t, FILE*);

int fgetc(FILE*);
int getc(FILE*);
int getchar(void);
int ungetc(int, FILE*);

int fputc(int, FILE*);
int putc(int, FILE*);
int putchar(int);

char* fgets(char*, int, FILE*);
char* gets(char*);

int fputs(const char*, FILE*);
int puts(const char*);

int printf(const char*, ...);
int fprintf(FILE*, const char*, ...);
int sprintf(char*, const char*, ...);
int snprintf(char*, size_t, const char*, ...);

/*int vprintf(const char*, va_list);
int vfprintf(FILE*, const char*, va_list);
int vsprintf(char*, const char*, va_list);
int vsnprintf(char*, size_t, const char*, va_list);*/

int scanf(const char*, ...);
int fscanf(FILE*, const char*, ...);
int sscanf(const char*, const char*, ...);
/*int vscanf(const char*, va_list);
int vfscanf(FILE*, const char*, va_list);
int vsscanf(const char*, const char*, va_list);*/

void perror(const char *);

int setvbuf(FILE*, char*, int, size_t);
void setbuf(FILE*, char*);

char* tmpnam(char*);
FILE* tmpfile(void);

#endif
