#define ACPI_MACHINE_WIDTH 64
#define ACPI_SINGLE_THREADED
#define ACPI_USE_LOCAL_CACHE
#define ACPI_INLINE inline
#define ACPI_USE_NATIVE_DIVIDE

#define DEBUGGER_THREADING DEBUGGER_SINGLE_THREADED

#ifdef ACPI_FULL_DEBUG
#define ACPI_DEBUG_OUTPUT
#define ACPI_DISASSEMBLER

// Depends on threading support
#define ACPI_DEBUGGER
#define ACPI_DBG_TRACK_ALLOCATIONS

#define ACPI_GET_FUNCTION_NAME __FUNCTION__
#else
#define ACPI_GET_FUNCTION_NAME ""
#endif

#define ACPI_PHYS_BASE 0x100000000

#include <stdint.h>
#include <stdarg.h>
#define AcpiOsPrintf printf
#define AcpiOsVprintf vprintf

struct acpi_table_facs;
uint32_t AcpiOsReleaseGlobalLock(struct acpi_table_facs* facs);
uint32_t AcpiOsAcquireGlobalLock(struct acpi_table_facs* facs);

#define ACPI_ACQUIRE_GLOBAL_LOCK(GLptr, Acquired) Acquired = AcpiOsAcquireGlobalLock(GLptr)
#define ACPI_RELEASE_GLOBAL_LOCK(GLptr, Pending) Pending = AcpiOsReleaseGlobalLock(GLptr)

