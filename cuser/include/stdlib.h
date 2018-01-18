#ifndef __STDLIB_H
#define __STDLIB_H

#include <__decls.h>
/* for recv0 */
#include <sb1.h>

__BEGIN_DECLS

/* malloc/free might not be implemented, depending on what you link against. */
extern void* malloc(size_t size);
extern void free(void* p);

static void abort(void) __attribute__((noreturn));
static void abort(void)
{
	for (;;) recv0(-1);
}

unsigned long int strtoul(const char *nptr, char **endptr, int base);

__END_DECLS

#endif /* __STDLIB_H */
