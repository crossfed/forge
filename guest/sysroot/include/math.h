#pragma once

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define FP_ILOGBNAN (-1 - (int)(((unsigned)-1) >> 1))
#define FP_ILOGB0 FP_ILOGBNAN

#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))

#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling 0

#define M_E 2.7182818284590452354
#define M_LOG2E 1.4426950408889634074
#define M_LOG10E 0.43429448190325182765
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_1_PI 0.31830988618379067154
#define M_2_PI 0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440

#if __FLT_EVAL_METHOD__ == 2
typedef long double float_t;
typedef long double double_t;
#elif __FLT_EVAL_METHOD__ == 1
typedef double float_t;
typedef double double_t;
#else
typedef float float_t;
typedef double double_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FORGE_CONTRACT_DECLARE_MATH(name)                                                                              \
   double name(double value);                                                                                          \
   float name##f(float value);                                                                                         \
   long double name##l(long double value)

FORGE_CONTRACT_DECLARE_MATH(acos);
FORGE_CONTRACT_DECLARE_MATH(acosh);
FORGE_CONTRACT_DECLARE_MATH(asin);
FORGE_CONTRACT_DECLARE_MATH(asinh);
FORGE_CONTRACT_DECLARE_MATH(atan);
FORGE_CONTRACT_DECLARE_MATH(atanh);
FORGE_CONTRACT_DECLARE_MATH(cbrt);
FORGE_CONTRACT_DECLARE_MATH(ceil);
FORGE_CONTRACT_DECLARE_MATH(cos);
FORGE_CONTRACT_DECLARE_MATH(cosh);
FORGE_CONTRACT_DECLARE_MATH(erf);
FORGE_CONTRACT_DECLARE_MATH(erfc);
FORGE_CONTRACT_DECLARE_MATH(exp);
FORGE_CONTRACT_DECLARE_MATH(exp2);
FORGE_CONTRACT_DECLARE_MATH(expm1);
FORGE_CONTRACT_DECLARE_MATH(fabs);
FORGE_CONTRACT_DECLARE_MATH(floor);
FORGE_CONTRACT_DECLARE_MATH(lgamma);
FORGE_CONTRACT_DECLARE_MATH(log);
FORGE_CONTRACT_DECLARE_MATH(log10);
FORGE_CONTRACT_DECLARE_MATH(log1p);
FORGE_CONTRACT_DECLARE_MATH(log2);
FORGE_CONTRACT_DECLARE_MATH(logb);
FORGE_CONTRACT_DECLARE_MATH(nearbyint);
FORGE_CONTRACT_DECLARE_MATH(rint);
FORGE_CONTRACT_DECLARE_MATH(round);
FORGE_CONTRACT_DECLARE_MATH(sin);
FORGE_CONTRACT_DECLARE_MATH(sinh);
FORGE_CONTRACT_DECLARE_MATH(sqrt);
FORGE_CONTRACT_DECLARE_MATH(tan);
FORGE_CONTRACT_DECLARE_MATH(tanh);
FORGE_CONTRACT_DECLARE_MATH(tgamma);
FORGE_CONTRACT_DECLARE_MATH(trunc);

#undef FORGE_CONTRACT_DECLARE_MATH

#define FORGE_CONTRACT_DECLARE_BINARY_MATH(name)                                                                       \
   double name(double left, double right);                                                                             \
   float name##f(float left, float right);                                                                             \
   long double name##l(long double left, long double right)

FORGE_CONTRACT_DECLARE_BINARY_MATH(atan2);
FORGE_CONTRACT_DECLARE_BINARY_MATH(copysign);
FORGE_CONTRACT_DECLARE_BINARY_MATH(fdim);
FORGE_CONTRACT_DECLARE_BINARY_MATH(fmax);
FORGE_CONTRACT_DECLARE_BINARY_MATH(fmin);
FORGE_CONTRACT_DECLARE_BINARY_MATH(fmod);
FORGE_CONTRACT_DECLARE_BINARY_MATH(hypot);
FORGE_CONTRACT_DECLARE_BINARY_MATH(nextafter);
FORGE_CONTRACT_DECLARE_BINARY_MATH(pow);
FORGE_CONTRACT_DECLARE_BINARY_MATH(remainder);

#undef FORGE_CONTRACT_DECLARE_BINARY_MATH

double fma(double left, double right, double addend);
float fmaf(float left, float right, float addend);
long double fmal(long double left, long double right, long double addend);

double frexp(double value, int* exponent);
float frexpf(float value, int* exponent);
long double frexpl(long double value, int* exponent);
double ldexp(double value, int exponent);
float ldexpf(float value, int exponent);
long double ldexpl(long double value, int exponent);
double modf(double value, double* integral);
float modff(float value, float* integral);
long double modfl(long double value, long double* integral);
double scalbn(double value, int exponent);
float scalbnf(float value, int exponent);
long double scalbnl(long double value, int exponent);
double scalbln(double value, long exponent);
float scalblnf(float value, long exponent);
long double scalblnl(long double value, long exponent);

int ilogb(double value);
int ilogbf(float value);
int ilogbl(long double value);
long lrint(double value);
long lrintf(float value);
long lrintl(long double value);
long long llrint(double value);
long long llrintf(float value);
long long llrintl(long double value);
long lround(double value);
long lroundf(float value);
long lroundl(long double value);
long long llround(double value);
long long llroundf(float value);
long long llroundl(long double value);

double nan(const char* tag);
float nanf(const char* tag);
long double nanl(const char* tag);
double nexttoward(double value, long double direction);
float nexttowardf(float value, long double direction);
long double nexttowardl(long double value, long double direction);
double remquo(double left, double right, int* quotient);
float remquof(float left, float right, int* quotient);
long double remquol(long double left, long double right, int* quotient);

extern int signgam;
double j0(double value);
float j0f(float value);
double j1(double value);
float j1f(float value);
double jn(int order, double value);
float jnf(int order, float value);
double y0(double value);
float y0f(float value);
double y1(double value);
float y1f(float value);
double yn(int order, double value);
float ynf(int order, float value);
double drem(double left, double right);
float dremf(float left, float right);
int finite(double value);
int finitef(float value);
double scalb(double value, double exponent);
float scalbf(float value, float exponent);
double significand(double value);
float significandf(float value);
double lgamma_r(double value, int* sign);
float lgammaf_r(float value, int* sign);
long double lgammal_r(long double value, int* sign);
void sincos(double value, double* sine, double* cosine);
void sincosf(float value, float* sine, float* cosine);
void sincosl(long double value, long double* sine, long double* cosine);
double exp10(double value);
float exp10f(float value);
long double exp10l(long double value);
double pow10(double value);
float pow10f(float value);
long double pow10l(long double value);

#ifdef __cplusplus
}
#endif

#define fpclassify(value) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, (value))
#define isfinite(value) __builtin_isfinite(value)
#define isinf(value) __builtin_isinf(value)
#define isnan(value) __builtin_isnan(value)
#define isnormal(value) __builtin_isnormal(value)
#define signbit(value) __builtin_signbit(value)
#define isgreater(left, right) __builtin_isgreater((left), (right))
#define isgreaterequal(left, right) __builtin_isgreaterequal((left), (right))
#define isless(left, right) __builtin_isless((left), (right))
#define islessequal(left, right) __builtin_islessequal((left), (right))
#define islessgreater(left, right) __builtin_islessgreater((left), (right))
#define isunordered(left, right) __builtin_isunordered((left), (right))
