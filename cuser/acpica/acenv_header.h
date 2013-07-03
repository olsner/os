#define ACPI_MACHINE_WIDTH 64
#define ACPI_SINGLE_THREADED
#define ACPI_DEBUG_OUTPUT
#define ACPI_DISASSEMBLER
#define ACPI_USE_LOCAL_CACHE
#define ACPI_INLINE inline
#define ACPI_USE_NATIVE_DIVIDE

// Depends on threading support
#define ACPI_DEBUGGER
#define DEBUGGER_THREADING DEBUGGER_SINGLE_THREADED
// Depends on debugger support
//#define ACPI_DBG_TRACK_ALLOCATIONS

#define ACPI_PHYS_BASE 0x100000000

#if 0
#define AcpiException(ModName, Line, Status, Format, ...) \
	AcpiOsPrintf(ACPI_MSG_EXCEPTION "%s, " Format, \
		AcpiFormatException(Status), \
		## __VA_ARGS__)
#endif

#define ACPI_GET_FUNCTION_NAME __FUNCTION__

#include <stdint.h>
#include <stdarg.h>
#define AcpiOsPrintf printf
#define AcpiOsVprintf vprintf

struct acpi_table_facs;
uint32_t AcpiOsReleaseGlobalLock(struct acpi_table_facs* facs);
uint32_t AcpiOsAcquireGlobalLock(struct acpi_table_facs* facs);

#define ACPI_ACQUIRE_GLOBAL_LOCK(GLptr, Acquired) Acquired = AcpiOsAcquireGlobalLock(GLptr)
#define ACPI_RELEASE_GLOBAL_LOCK(GLptr, Pending) Pending = AcpiOsReleaseGlobalLock(GLptr)

