#include <inttypes.h>
#include <stdio.h>

int main()
{
	uint64_t rflags;
	__asm__ __volatile__ ("pushf; popq %0" : "=g" (rflags) ::"memory");
	printf("rFLAGS: %016llx\n", rflags);
#define BIT(name, number) \
	printf("\t%s: %d\n", name, rflags & (1 << number) ? 1 : 0)

	const char* bits[] = {
		"CF",
		"Reserved (1)",
		"PF",
		"Reserved (0)",
		"AF",
		"Reserved (0)",
		"ZF (Zero)",
		"SF (Sign)",
		"TF (Trap)",
		"IF (Interrupt)",
		"DF (Direction)",
		"OF (Overflow)",
		"IOPL LSB",
		"IOPL MSB",
		"NT"
	};
	int i;
	for (i=0;i<sizeof(bits)/sizeof(*bits);i++)
		BIT(bits[i], i);
}
