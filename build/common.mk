SYSTEM := $(shell uname -s)

ifeq ($(SYSTEM), Darwin)
FILE_SIZE = stat -f%z
else
FILE_SIZE = stat -c%s
endif

# Default is not verbose, i.e. VERBOSE is empty.
ifeq ($(VERBOSE),YES)
CP=cp -v
else
CP=cp
endif

ifneq ($(VERBOSE),YES)
HUSH_AS     = @echo -e ' [AS]\t'$@;
HUSH_ASM    = @echo -e ' [ASM]\t'$@;
# Not useful to know about really - implied by CC/CXX so might as well be implied by AS/ASM too
#HUSH_ASM_DEP=@echo -e ' [DEP]\t'$@;
HUSH_ASM_DEP= @
HUSH_CC     = @echo -e ' [CC]\t'$@;
HUSH_CXX    = @echo -e ' [CXX]\t'$@;
HUSH_LD     = @echo -e ' [LD]\t'$@;
HUSH_OBJCOPY= @echo -e ' [OBJCOPY]\t'$@;

SIZE_ASM=@echo -e ' [ASM]\t'$@: `$(FILE_SIZE) $@` bytes
SIZE_LD= @echo -e ' [LD]\t'$@: `$(FILE_SIZE) $@` bytes
SIZE_OBJCOPY= @echo -e ' [OBJCOPY]\t'$@: `$(FILE_SIZE) $@` bytes
endif

ifeq ($(LTO), YES)
HUSH_LD_LTO = +$(HUSH_LD)
else
HUSH_LD_LTO = $(HUSH_LD)
endif
