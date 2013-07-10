.PHONY: all
all:

ifeq ($(VERBOSE),YES)
CP=cp -v
else
HUSH_ASM=@echo ' [ASM]\t'$@;
#HUSH_ASM_DEP=@echo ' [DEP]\t'$@;
HUSH_ASM_DEP=@
HUSH_CC= @echo ' [CC]\t'$@;
HUSH_CXX=@echo ' [CXX]\t'$@;
HUSH_LD= @echo ' [LD]\t'$@;
endif

OUTDIR ?= out
ACPICA_OUT := $(OUTDIR)

ACPICA = ../../acpica
ACPICA_SRC =            $(ACPICA)/source
ACPICA_COMMON =         $(ACPICA_SRC)/common
ACPICA_TOOLS =          $(ACPICA_SRC)/tools
ACPICA_OSL =            $(ACPICA_SRC)/os_specific/service_layers
ACPICA_CORE =           $(ACPICA_SRC)/components
ACPICA_INCLUDE =        $(ACPICA_SRC)/include
ACPICA_DEBUGGER =       $(ACPICA_CORE)/debugger
ACPICA_DISASSEMBLER =   $(ACPICA_CORE)/disassembler
ACPICA_DISPATCHER =     $(ACPICA_CORE)/dispatcher
ACPICA_EVENTS =         $(ACPICA_CORE)/events
ACPICA_EXECUTER =       $(ACPICA_CORE)/executer
ACPICA_HARDWARE =       $(ACPICA_CORE)/hardware
ACPICA_NAMESPACE =      $(ACPICA_CORE)/namespace
ACPICA_PARSER =         $(ACPICA_CORE)/parser
ACPICA_RESOURCES =      $(ACPICA_CORE)/resources
ACPICA_TABLES =         $(ACPICA_CORE)/tables
ACPICA_UTILITIES =      $(ACPICA_CORE)/utilities

UT_SRCS := \
	xface xferror xfinit \
	excep debug global alloc clib track decode string math cache mutex lock \
	delete object state misc address ownerid error osi eval ids copy predef \
	buffer resrc init
TB_SRCS := \
	xface xfload instal utils print fadt find xfroot
EV_SRCS := \
	xface glock xfevnt gpeblk event region handler misc gpe rgnini gpeutil \
	sci xfregn gpeinit
NS_SRCS := \
	xfeval access utils load object walk names eval arguments predef alloc \
	init parse dump search xfname xfobj prepkg repair repair2 convert
EX_SRCS := \
	utils mutex resnte system dump region prep resop resolv convrt create \
	names field store fldio debug oparg1 oparg2 oparg3 oparg6 storen misc \
	config storob
DB_SRCS := \
	xface input utils histry method names fileio exec disply cmds names stats \
	convert
DS_SRCS := \
	init wscope wstate opcode wload mthdat object utils field wload2 method \
	wexec args control
HW_SRCS := \
	xface acpi gpe pci regs xfsleep esleep sleep valid
PS_SRCS := \
	xface scope utils walk tree opinfo parse opcode args loop object
DM_SRCS := \
	walk object utils opcode names buffer deferred resrc resrcs resrcl resrcl2
RS_SRCS := \
	xface create dump info list dumpinfo utils calc memory io irq serial misc \
	addr

ACPI_SRCS := \
	$(UT_SRCS:%=$(ACPICA_UTILITIES)/ut%.c) \
	$(TB_SRCS:%=$(ACPICA_TABLES)/tb%.c) \
	$(EV_SRCS:%=$(ACPICA_EVENTS)/ev%.c) \
	$(NS_SRCS:%=$(ACPICA_NAMESPACE)/ns%.c) \
	$(EX_SRCS:%=$(ACPICA_EXECUTER)/ex%.c) \
	$(DB_SRCS:%=$(ACPICA_DEBUGGER)/db%.c) \
	$(DS_SRCS:%=$(ACPICA_DISPATCHER)/ds%.c) \
	$(HW_SRCS:%=$(ACPICA_HARDWARE)/hw%.c) \
	$(PS_SRCS:%=$(ACPICA_PARSER)/ps%.c) \
	$(DM_SRCS:%=$(ACPICA_DISASSEMBLER)/dm%.c) \
	$(RS_SRCS:%=$(ACPICA_RESOURCES)/rs%.c) \

ACPI_OBJS := $(ACPI_SRCS:$(ACPICA)/%.c=$(ACPICA_OUT)/%.o)
SRCS := acpica.c osl.c malloc.c pci.c
OBJS := $(SRCS:%.c=$(OUTDIR)/%.o)
OBJS += $(OUTDIR)/printf.o

DEPS := $(OBJS:.o=.d) $(ACPI_OBJS:.o=.d)
-include $(DEPS)

USER_CFLAGS = -ffreestanding -g -Os -W -Wall -Wextra -march=native -std=gnu99
USER_CFLAGS += -Wno-unused-function -Wno-unused-parameter
USER_CFLAGS += -ffunction-sections -fdata-sections
USER_CFLAGS += -I.. -I. -I$(ACPICA_INCLUDE)
USER_CFLAGS += -DACENV_HEADER='"acenv_header.h"'
USER_CFLAGS += -DACPI_FULL_DEBUG

all: $(ACPICA_OUT)/acpica.mod $(ACPICA_OUT)/acpica.elf

$(ACPICA_OUT)/%.o: $(ACPICA)/%.c
	@mkdir -p $(@D)
	$(HUSH_CC) $(CC) $(USER_CFLAGS) -c -MP -MMD -o $@ $<

$(OUTDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(HUSH_CC) $(CC) $(USER_CFLAGS) -c -MP -MMD -o $@ $<

$(OUTDIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(HUSH_CC) $(CXX) $(USER_CFLAGS) -c -MP -MMD -o $@ $<

LDFLAGS := --check-sections --warn-section-align 
LDFLAGS += --gc-sections

$(ACPICA_OUT)/acpica.mod: ../linker.ld $(OBJS) $(ACPI_OBJS)
	@mkdir -p $(@D)
	$(HUSH_LD) $(LD) -o $@ $(LDFLAGS) -T $^ -Map $(OUTDIR)/acpica.map

$(ACPICA_OUT)/acpica.elf: ../linker.ld $(OBJS) $(ACPI_OBJS)
	@mkdir -p $(@D)
	$(HUSH_LD) $(LD) --oformat elf64-x86-64 -o $@ $(LDFLAGS) -T $^

.PHONY: clean
clean:
	rm -fr out
