/* In hosted environment, GCC's stdint.h includes libc's stdint.h, which is
 * this. To avoid reimplementing stuff, just include the gcc header :) */
#include <stdint-gcc.h>
