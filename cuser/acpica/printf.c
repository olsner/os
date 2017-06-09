#include "common.h"
#include "acpi.h"
#include "accommon.h"

#define fputc_unlocked(x, file) putchar(x)
#define fwrite_unlocked(buf, n, s, file) putchars(buf, (n) * (s))
#define flockfile(file) (void)0

#undef isdigit
static int isdigit(int c) {
	return c >= '0' && c <= '9';
}

static void putchars(const char* buf, size_t n) {
	while (n--) {
		putchar(*buf++);
	}
}

static long int strtol(const char* p, char** end, int base) {
	// FIXME This is the last of the ACPICA dependencies. plzfix.
	return strtoul(p, end, base);
}

static void format_num(int width, bool leading_zero, int base, bool show_base, uintmax_t num)
{
	if (show_base)
	{
		assert(base == 16);
		fputc_unlocked('0', file);
		fputc_unlocked('x', file);
	}
	char buf[32];
	memset(buf, 0, sizeof(buf));
	size_t len = 0;
	do
	{
		int i = num % base;
		buf[len++] = "0123456789abcdef"[i >= 0 ? i : -i];
		num /= base;
	}
	while (num);
	if (width)
	{
		int c = leading_zero ? '0' : ' ';
		while (len < (size_t)width--)
		{
			fputc_unlocked(c, file);
		}
	}
	while (len--)
	{
		fputc_unlocked(buf[len], file);
	}
}

static void format_snum(int width, bool leading_zero, int64_t num)
{
	if (num < 0)
	{
		num = -num;
		fputc_unlocked('-', file);
	}
	format_num(width, leading_zero, 10, false, num);
}

static const char* read_width(const char* fmt, int* width)
{
	//errno = 0;
	char* endptr = NULL;
	*width = strtol(fmt, &endptr, 10);
	//assert(!errno);
	return endptr;
}

static void format_str(const char* s, bool left, size_t width, size_t maxwidth)
{
	size_t len = strlen(s);
	if (maxwidth && len > maxwidth) len = maxwidth;
	if (left) {
		putchars(s, len);
		while (width && len++ < width) putchar(' ');
	} else {
		while (width-- > len) putchar(' ');
		putchars(s, len);
	}
}

void vprintf(const char* fmt, va_list ap)
{
	flockfile(file);
	while (*fmt)
	{
		const char* nextformat = strchr(fmt, '%');
		if (!nextformat)
		{
			fwrite_unlocked(fmt, 1, strlen(fmt), file);
			break;
		}
		else
		{
			fwrite_unlocked(fmt, 1, nextformat - fmt, file);
			fmt = nextformat + 1;
		}
		bool is_long = false;
		bool is_size = false;
		bool leading_zero = false;
		bool sign = true;
		bool show_base = false;
		bool left_align = false;
		int width = 0;
		bool after_point = false;
		int precision = 0;
		int base = 10;
		for (;;)
		{
			switch (*fmt++)
			{
			case '%':
				fputc_unlocked('%', file);
				break;
			case 'c':
				fputc_unlocked(va_arg(ap, int), file);
				break;
			case 's':
			{
				const char* arg = va_arg(ap, const char*);
				if (arg)
					format_str(arg, left_align, width, precision);
				else
					fwrite_unlocked("(null)", 1, sizeof("(null)")-1, file);
				break;
			}
			case 'x':
			case 'X':
				base = 16;
				__attribute__((fallthrough));
			case 'u':
				sign = false;
				__attribute__((fallthrough));
			case 'd':
#define format_signed(type) format_snum(width, leading_zero, va_arg(ap, type))
#define format_unsigned(type) format_num(width, leading_zero, base, show_base, va_arg(ap, type))
#define format_num_type(type) sign ? format_signed(type) : format_unsigned(unsigned type)
				if (is_long)
					format_num_type(long);
				else if (is_size)
					format_unsigned(size_t);
				else
					format_num_type(int);
				break;
			case 'p':
				format_num(0, false, 16, true, (uintptr_t)va_arg(ap, void*));
				break;
			case 'l':
				is_long = true;
				continue;
			case 'z':
				is_size = true;
				continue;
			case '#':
				show_base = true;
				continue;
			case '.':
				after_point = true;
				continue;
			case '0':
				leading_zero = true;
				fmt = read_width(fmt, &width);
				continue;
			case '-':
				left_align = true;
				continue;
			case '+':
				continue;
			default:
				if (isdigit(fmt[-1]))
				{
					fmt = read_width(fmt - 1, after_point ? &precision : &width);
					continue;
				}
				fputc_unlocked('%', file);
				fputc_unlocked(fmt[-1], file);
				return;
			}
			break;
		}
	}
	//funlockfile(file);
	//fflush(file);
	//fflush(stderr); // HACK
}

void printf(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

