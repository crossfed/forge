#pragma once

#ifdef __cplusplus
extern "C" [[noreturn]] void abort(void);
#else
_Noreturn void abort(void);
#endif

#ifdef NDEBUG
#define assert(condition) ((void)0)
#else
#define assert(condition) ((condition) ? (void)0 : abort())
#endif
