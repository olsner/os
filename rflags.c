#include <inttypes.h>
#include <stdio.h>

int main()
{
	uint64_t rflags;
	__asm__ __volatile__ ("pushf; popq %0" : "=g" (rflags) ::"memory");
	printf("rFLAGS: %016llx\n", (unsigned long long)rflags);
#define BIT(name, number) \
	printf("#%d\t%s:\t%d\n", number, name, rflags & (1 << number) ? 1 : 0)

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
		"NT",
		"Reserved (0)",
		"RF (Resume)",
		"VM",
		"AC (Alignment check)",
		"VIF",
		"VIP",
		"ID"
	};
	int i;
	for (i=0;i<sizeof(bits)/sizeof(*bits);i++)
		BIT(bits[i], i);
}
