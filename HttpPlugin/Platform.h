#include <stdint.h>

#if UINTPTR_MAX == 0xffffffffffffffff
#define ENV64
#else
#define ENV32
#endif