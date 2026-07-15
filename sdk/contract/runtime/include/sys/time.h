#pragma once

#include <time.h>

struct timeval {
   time_t tv_sec;
   long tv_usec;
};

struct timezone {
   int tz_minuteswest;
   int tz_dsttime;
};

#ifdef __cplusplus
extern "C" {
#endif

int gettimeofday(struct timeval* value, void* zone);

#ifdef __cplusplus
}
#endif
