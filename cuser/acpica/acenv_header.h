#include <stdint.h>
#include <stdio.h>

#define ACPI_MACHINE_WIDTH __INTPTR_WIDTH__
#define ACPI_SINGLE_THREADED
#define ACPI_USE_LOCAL_CACHE
#define ACPI_USE_NATIVE_DIVIDE
#define ACPI_USE_SYSTEM_CLIBRARY
#define ACPI_USE_STANDARD_HEADERS

#define DEBUGGER_THREADING DEBUGGER_SINGLE_THREADED

// Doesn't seem to be possible to build without this enabled? Are you supposed
// to remove the debugger source files when disabling it?
#define ACPI_FULL_DEBUG
#ifdef ACPI_FULL_DEBUG
#define ACPI_DEBUG_OUTPUT
#define ACPI_DEBUGGER 1
#define ACPI_DISASSEMBLER 1
#endif

#undef ACPI_GET_FUNCTION_NAME
#ifdef ACPI_FULL_DEBUG
#define ACPI_DBG_TRACK_ALLOCATIONS
#define ACPI_GET_FUNCTION_NAME __FUNCTION__
#else
#define ACPI_GET_FUNCTION_NAME ""
#endif

static const uint64_t ACPI_PHYS_BASE = 0x100000000;

// Prevent ACPI from redeclaring printf
// (without -ffreestanding, that will trigger -Wbuiltin-declaration-mismatch)
#define ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsPrintf
#define ACPI_USE_ALTERNATE_PROTOTYPE_AcpiOsVprintf
#define AcpiOsPrintf printf
#define AcpiOsVprintf vprintf

struct acpi_table_facs;
uint32_t AcpiOsReleaseGlobalLock(struct acpi_table_facs* facs);
uint32_t AcpiOsAcquireGlobalLock(struct acpi_table_facs* facs);

#define ACPI_ACQUIRE_GLOBAL_LOCK(GLptr, Acquired) Acquired = AcpiOsAcquireGlobalLock(GLptr)
#define ACPI_RELEASE_GLOBAL_LOCK(GLptr, Pending) Pending = AcpiOsReleaseGlobalLock(GLptr)

void AcpiOsFlushCache(void);
#define ACPI_FLUSH_CPU_CACHE AcpiOsFlushCache

#define COMPILER_DEPENDENT_UINT64 uint64_t
#define COMPILER_DEPENDENT_UINT32 uint32_t

// ACPICA by default defines this to void*, so define this to the proper type.
// Note that using uintptr_t instead of void* actually hides a lot of constness
// issues in ACPICA code. So arguably it'd be better to leave this defined to
// the wrong type...
#define ACPI_UINTPTR_T uintptr_t
