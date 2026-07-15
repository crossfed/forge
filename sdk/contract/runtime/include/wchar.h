#pragma once

#include <stddef.h>
#include <stdint.h>

typedef uint32_t wint_t;
typedef struct {
   uint32_t state;
} mbstate_t;

#define WEOF ((wint_t)-1)
