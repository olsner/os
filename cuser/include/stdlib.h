#ifndef __STDLIB_H
#define __STDLIB_H

#include <__decls.h>
#include <stddef.h>

__BEGIN_DECLS

/* malloc/free might not be implemented, depending on what you link against. */
void* malloc(size_t size);
void free(void* p);

void abort(void) __attribute__((noreturn));

unsigned long int strtoul(const char *nptr, char **endptr, int base);
long int strtol(const char* p, char** end, int base);
int atoi(const char *s);


__END_DECLS

#endif /* __STDLIB_H */
