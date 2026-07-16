#pragma once

typedef struct {
   unsigned int state;
} fenv_t;

typedef unsigned int fexcept_t;

#define FE_ALL_EXCEPT 0
#define FE_TONEAREST 0
