// compiler/platform-specific definitions for lwip

#ifndef __ARCH_CC_H__
#define __ARCH_CC_H__

// TODO Stop including common.h here, just include necessary "official" headers
#include "common.h"
#include <stdint.h>

#define BYTE_ORDER  LITTLE_ENDIAN

typedef uint8_t     u8_t;
typedef int8_t      s8_t;
typedef uint16_t    u16_t;
typedef int16_t     s16_t;
typedef uint32_t    u32_t;
typedef int32_t     s32_t;

typedef uintptr_t   mem_ptr_t;

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

/* Compiler hints for packing structures */
#define PACK_STRUCT_FIELD(x)    x
#define PACK_STRUCT_STRUCT  __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

/* Plaform specific diagnostic output */
#define LWIP_PLATFORM_DIAG(x) printf x
#define LWIP_PLATFORM_ASSERT(x) assert_failed(__FILE__, __LINE__, x)

#endif /* __ARCH_CC_H__ */
