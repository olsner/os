// compiler/platform-specific definitions for lwip

#ifndef __ARCH_CC_H__
#define __ARCH_CC_H__

#include <assert.h> // for assert_failed, non-standard function but we assume it's in assert.h
#include <stdint.h>
#include <string.h>

#define BYTE_ORDER  LITTLE_ENDIAN

#define LWIP_ERR_T  int
#define LWIP_NO_INTTYPES_H 1

#define X8_F  "02x"

#define U16_F "u"
#define S16_F "d"
#define X16_F "x"

#define U32_F "u"
#define S32_F "d"
#define X32_F "x"

#define SZT_F "zu"

/* Use our assert_failed instead of LWIP's print/flush/abort. */
#define LWIP_PLATFORM_ASSERT(x) assert_failed(__FILE__, __LINE__, x)

// Saves about a kilobyte over the default out-of-line implementations
#define lwip_htons __builtin_bswap16
#define lwip_htonl __builtin_bswap32

#endif /* __ARCH_CC_H__ */
