#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

typedef int32_t i32;
typedef uint32_t u32;

u32 parse_hex(const char* arg)
{
	char temp[9];
	char* p = temp, *pend = temp+sizeof(temp);
	memset(p, 0, pend-p);
	if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X'))
		arg+=2;
	while (*arg && *arg != ':')
	{
		if (*arg != '_')
			*p++ = *arg;
		arg++;
	}

	return strtoul(temp, NULL, 16);
}

static const char* regs[] = { "eax","ebx","ecx","edx",NULL };
void parse_regbit(const char* bitarg, int* reg, int* bit)
{
	const char* colon = strchr(bitarg, ':');
	if (colon && colon-bitarg != 3)
	{
		fprintf(stderr, "Invalid register/bit argument: expected 3 chars\n");
		abort();
	}
	const char** regsp = regs;
	while (const char* regname = *regsp++)
	{
		if (!strncasecmp(regname, bitarg, 3))
			*reg = regsp-regs-1;
	}
	if (colon)
	{
		*bit = atoi(colon+1);
	}
}

void cpuid(u32 cpuid_num, u32 cpuid_num2, u32* data)
{
	__asm__ __volatile__(
		"cpuid"
		: "=a" (data[0])
		, "=b" (data[1])
		, "=c" (data[2])
		, "=d" (data[3])
		: "a" (cpuid_num)
		, "c" (cpuid_num2)
		: );
}

void print_info(u32 num, u32 num2, u32* data, int reg, int bit)
{
	printf("CPUID %08x:%08x:\n", num, num2);
	for (int i=0;i<4;i++)
		printf("%s: %08x\n", regs[i], data[i]);
	printf("\n");
	if (reg >= 0 && bit >= 0)
		printf("Reg %s bit %d: %s\n", regs[reg], bit, (data[reg] & (1 << bit)) ? "set" : "clear");
}

int main(int argc, const char *const argv[])
{
	const char* idarg = NULL;  // 8-digit hexadecimal number, _'s ignored
	const char* bitarg = NULL; // reg[:bit], if not given print everything.

	if (argc >= 2)
		idarg = argv[1];
	if (argc >= 3)
		bitarg = argv[2];

	u32 cpuid_num = 0, cpuid_num2 = 0;
	if (idarg)
	{
		cpuid_num = parse_hex(idarg);
		if (const char *col = strchr(idarg, ':'))
		{
			cpuid_num2 = parse_hex(col + 1);
		}
	}
	int bitnum = -1;
	int reg = -1;
	if (bitarg) parse_regbit(bitarg, &reg, &bitnum);

	printf("cpuid'ing %08x %d bit %d\n", cpuid_num, reg, bitnum);

	u32 data[4];
	cpuid(cpuid_num, cpuid_num2, data);
	print_info(cpuid_num, cpuid_num2, data, reg, bitnum);
}
