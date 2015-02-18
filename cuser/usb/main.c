#include "common.h"

#define log printf
#if 1
#define debug log
#else
#define debug(...) (void)0
#endif

// "main" USB driver - this talks to both host controllers and USB device
// drivers and manages communication between them.

void start()
{
	__default_section_init();
	for (;;);
}
