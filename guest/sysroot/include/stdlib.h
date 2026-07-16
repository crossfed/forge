#pragma once

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

typedef struct {
   int quot;
   int rem;
} div_t;

typedef struct {
   long quot;
   long rem;
} ldiv_t;

typedef struct {
   long long quot;
   long long rem;
} lldiv_t;

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
[[noreturn]] void abort(void);
[[noreturn]] void _Exit(int status);
[[noreturn]] void exit(int status);
#else
_Noreturn void abort(void);
_Noreturn void _Exit(int status);
_Noreturn void exit(int status);
#endif

void* malloc(size_t size);
void* calloc(size_t count, size_t size);
void* realloc(void* value, size_t size);
void free(void* value);
void* aligned_alloc(size_t alignment, size_t size);

int abs(int value);
long labs(long value);
long long llabs(long long value);
div_t div(int numerator, int denominator);
ldiv_t ldiv(long numerator, long denominator);
lldiv_t lldiv(long long numerator, long long denominator);

double atof(const char* value);
int atoi(const char* value);
long atol(const char* value);
long long atoll(const char* value);
double strtod(const char* value, char** end);
float strtof(const char* value, char** end);
long double strtold(const char* value, char** end);
long strtol(const char* value, char** end, int base);
long long strtoll(const char* value, char** end, int base);
unsigned long strtoul(const char* value, char** end, int base);
unsigned long long strtoull(const char* value, char** end, int base);

void* bsearch(const void* key,
              const void* base,
              size_t count,
              size_t size,
              int (*compare)(const void*, const void*));
void qsort(void* base, size_t count, size_t size, int (*compare)(const void*, const void*));
char* getenv(const char* name);

#ifdef __cplusplus
}
#endif
