/* Note: the include guard is not covering the whole header - the assert macro
 * shall be redefined according to the current state of NDEBUG each time
 * assert.h is included. */
#ifndef __ASSERT_H
#define __ASSERT_H

#include <__decls.h>
#include <stdio.h>
#include <stdlib.h>

__BEGIN_DECLS

static void assert_failed(const char* file, int line, const char* msg) __attribute__((noreturn));
static void assert_failed(const char* file, int line, const char* msg) {
	printf("%s:%d: ASSERT FAILED (%s)\n", file, line, msg);
	abort();
}

__END_DECLS

#endif /* __ASSERT_H */

// TODO: If not NDEBUG

#undef assert
#define assert(X) \
	do { if (!(X)) assert_failed(__FILE__, __LINE__, #X); } while (0)

