#pragma once

struct lconv {
   char* decimal_point;
};

#define LC_ALL 0

#ifdef __cplusplus
extern "C" {
#endif

struct lconv* localeconv(void);
char* setlocale(int category, const char* locale);

#ifdef __cplusplus
}
#endif
