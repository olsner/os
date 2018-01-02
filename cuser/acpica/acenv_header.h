#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ACPI_MACHINE_WIDTH __INTPTR_WIDTH__
#define ACPI_SINGLE_THREADED
#define ACPI_USE_LOCAL_CACHE
#define ACPI_USE_NATIVE_DIVIDE

#define DEBUGGER_THREADING DEBUGGER_SINGLE_THREADED

#undef ACPI_GET_FUNCTION_NAME
#ifdef ACPI_FULL_DEBUG
//#define ACPI_DBG_TRACK_ALLOCATIONS
#define ACPI_GET_FUNCTION_NAME __FUNCTION__
#else
#define ACPI_GET_FUNCTION_NAME ""
#endif

static const uintptr_t ACPI_PHYS_BASE = 0x1000000;

#define AcpiOsPrintf printf
#define AcpiOsVprintf vprintf

struct acpi_table_facs;
uint32_t AcpiOsReleaseGlobalLock(struct acpi_table_facs* facs);
uint32_t AcpiOsAcquireGlobalLock(struct acpi_table_facs* facs);

#define ACPI_ACQUIRE_GLOBAL_LOCK(GLptr, Acquired) Acquired = AcpiOsAcquireGlobalLock(GLptr)
#define ACPI_RELEASE_GLOBAL_LOCK(GLptr, Pending) Pending = AcpiOsReleaseGlobalLock(GLptr)

#define COMPILER_DEPENDENT_UINT64 uint64_t
#define COMPILER_DEPENDENT_UINT32 uint32_t
