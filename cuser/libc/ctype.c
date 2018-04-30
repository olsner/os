#include <ctype.h>
#include <string.h>

int isdigit(int c) {
	return c >= '0' && c <= '9';
}

int isxdigit(int c) {
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int isspace(int c) {
    return !!strchr(" \t\r\n\v\f", c);
}

int isprint(int c) {
    return (unsigned)c-0x20 < 0x5f;
}

int tolower(int c) {
    return isupper(c) ? c ^ 0x20 : c;
}

int toupper(int c) {
    return islower(c) ? c ^ 0x20 : c;
}

int islower(int c) {
    return c >= 'a' && c <= 'z';
}

int isupper(int c) {
    return c >= 'A' && c <= 'Z';
}

int isalpha(int c) {
    return islower(c) || isupper(c);
}
