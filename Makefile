# Makefile for asmos (SB1?). Copyright by Simon Brenner, 2002-2013

.PHONY: all clean install commit

include build/common.mk
include build/makejobs.mk

CCACHE ?= ccache
ifneq ($(USE_CROSS),NO)
CROSS := x86_64-elf-
CROSSDIR := $(CURDIR)/toolchain/cross-7.3.0
export PATH := $(PATH):$(CROSSDIR)/bin
else
CROSS :=
endif
CC := $(CROSS)gcc
CXX := $(CROSS)g++
HOST_CC := gcc
HOST_CXX := g++

export LD := $(CROSS)ld
export OBJCOPY := $(CROSS)objcopy
export AS := $(CROSS)as
export CC := $(CCACHE) $(CC)
export CXX := $(CCACHE) $(CXX)

YASM ?= yasm/yasm
# Dependency to add to targets that need yasm - if using the in-tree yasm make
# sure it's built before using it for build products.
ifeq (yasm, $(YASM))
YASMDEP :=
else
YASMDEP := $(YASM)
endif
YASMFLAGS = -Werror -i $(ASMDIR) -i include

OUTDIR       := out
# Interesting experiment, but not very useful :) Makes modules slightly smaller
# (except for lwip), but ACPI actually relies on 64-bit pointers to allow it to
# access high physical memory more conveniently.
# Maybe something to revive if we can set different flags per cuser program,
# e.g. 64-bit ACPICA but 32-bit everything else since they don't need a lot of
# memory anyway.
ifeq ($(X32), YES)
OUTDIR       := out/x32
endif
ACPICA_OUT   := $(OUTDIR)/cuser/acpica
ACPICA       := acpica
LWIP_OUT     := $(OUTDIR)/cuser/lwip
LWIP         := lwip
GRUBDIR      := $(OUTDIR)/grub
ASMDIR       := kasm
MOD_ASMFILES := user/newproc.asm user/gettime.asm user/loop.asm user/shell.asm
MOD_ASMFILES += user/test_puts.asm user/test_xmm.asm
MOD_ASMFILES += kern/console.asm kern/pic.asm kern/irq.asm
ASMFILES     := $(ASMDIR)/kstart.asm $(MOD_ASMFILES)
MOD_CFILES   := cuser/helloworld.c cuser/zeropage.c
MOD_CFILES   += cuser/test_maps.c cuser/e1000.c cuser/apic.c cuser/timer_test.c
MOD_CFILES   += cuser/bochsvga.c cuser/fbtest.c cuser/acpi_debugger.c
MOD_CFILES   += cuser/ioapic.c
MOD_OFILES   := $(MOD_CFILES:%.c=$(OUTDIR)/%.o)
MOD_ELFS     := $(MOD_CFILES:%.c=$(OUTDIR)/%.elf)
MOD_ELFS     += $(OUTDIR)/cuser/acpica.elf $(OUTDIR)/cuser/lwip.elf
MODFILES     := $(MOD_ASMFILES:%.asm=$(GRUBDIR)/%.mod) $(MOD_CFILES:%.c=$(GRUBDIR)/%.mod) $(GRUBDIR)/cuser/acpica.mod $(GRUBDIR)/cuser/lwip.mod
DEPFILES     := $(ASMFILES:%.asm=$(OUTDIR)/%.d) $(MOD_OFILES:.o=.d)
ASMOUTS      := \
	$(GRUBDIR)/kstart.b \
	$(MODFILES) \
	$(ASMFILES:%.asm=$(OUTDIR)/%.b) \
	$(ASMFILES:.asm=.map) $(ASMFILES:.asm=.lst) \
	$(DEPFILES)
UTIL_BINS    := utils/cpuid utils/rflags

all: $(OUTDIR)/grub.iso
all: $(MOD_ELFS)
all: $(UTIL_BINS)

.SECONDARY: $(ASMFILES:%.asm=$(OUTDIR)/%.b) $(MOD_OFILES)

clean::
	rm -fr $(OUTDIR)
	rm -f cpuid rflags $(UTIL_BINS)

%: %.cpp
	$(HUSH_CXX) $(HOST_CXX) $(CXXFLAGS) -o $@ $<

%: %.c
	$(HUSH_CC) $(HOST_CC) $(CFLAGS) -o $@ $<

-include $(DEPFILES)

$(OUTDIR)/%.d: %.asm $(YASMDEP)
	@mkdir -p $(@D)
	$(HUSH_ASM_DEP) $(YASM) $(YASMFLAGS) -e -M $< -o $(@:.d=.b) > $(@)

$(OUTDIR)/%.b: %.asm $(OUTDIR)/%.d $(YASMDEP)
	@mkdir -p $(@D)
	$(HUSH_ASM) $(YASM) $(YASMFLAGS) -f bin $< -o $@ -L nasm -l $(OUTDIR)/$*.lst --mapfile=$(OUTDIR)/$*.map
	$(SIZE_ASM)

-include $(OUTDIR)/start32.d

$(OUTDIR)/start32.o: kasm/start32.asm $(YASMDEP)
	@mkdir -p $(@D)
	$(HUSH_ASM_DEP) $(YASM) $(YASMFLAGS) -e -M $< -o $@ > $(@:.o=.d)
	$(HUSH_ASM) $(YASM) $(YASMFLAGS) -f elf64 -g dwarf2 $< -o $@ -L nasm -l $(OUTDIR)/start32.lst
	$(SIZE_ASM)

$(OUTDIR)/kstart.b: $(INLINE_MODULES)

%.asm.pp: %.asm $(YASMDEP)
	$(YASM) -i . -f bin -o $@ -e -L nasm $<

$(GRUBDIR)/%.b: $(OUTDIR)/kasm/%.b
	@mkdir -p $(@D)
	@$(CP) $< $@

$(GRUBDIR)/%.mod: $(OUTDIR)/%.b
	@mkdir -p $(@D)
	@$(CP) $< $@

GRUB_MODULES = --modules="boot multiboot"

GRUB_CFG = $(GRUBDIR)/boot/grub/grub.cfg

USER_CFLAGS := -std=gnu11
USER_CFLAGS += -g -Os -march=sandybridge -mno-avx
USER_CFLAGS += -ffunction-sections -fdata-sections

USER_CFLAGS += -W -Wall -Wextra -Werror
# Enable useful warnings
USER_CFLAGS += -Wstrict-prototypes -Wmissing-prototypes
# Disable some less-than-useful warnings enabled by -Wall etc
USER_CFLAGS += -Wno-unused-function -Wno-unused-parameter

USER_CFLAGS += -Icuser/include

LDFLAGS := --check-sections
LDFLAGS += --gc-sections
USER_LDFLAGS = $(LDFLAGS)

LD_ELF_FORMAT := elf64-x86-64
YASM_ELF_FORMAT := elf64

ifeq ($(X32), YES)
USER_CFLAGS += -mx32
USER_LDFLAGS += -melf32_x86_64
LD_ELF_FORMAT := elf32-x86-64
YASM_ELF_FORMAT := elfx32
endif

USER_LDFLAGS += --oformat $(LD_ELF_FORMAT) -Map $(@:.elf=.map)

$(OUTDIR)/cuser/%.o: cuser/%.c
	@mkdir -p $(@D)
	$(HUSH_CC) $(CC) $(USER_CFLAGS) -c -MP -MMD -o $@ $<

$(OUTDIR)/cuser/%.o: cuser/%.cpp
	@mkdir -p $(@D)
	$(HUSH_CC) $(CC) $(USER_CFLAGS) -c -MP -MMD -o $@ $<

$(OUTDIR)/cuser/%.o: %.c
	@mkdir -p $(@D)
	$(HUSH_CC) $(CC) $(USER_CFLAGS) -c -MP -MMD -o $@ $<

$(GRUBDIR)/%.mod: $(OUTDIR)/%.elf
	@mkdir -p $(@D)
	$(HUSH_OBJCOPY) $(OBJCOPY) -O binary $< $@
	$(SIZE_OBJCOPY)

$(OUTDIR)/%.elf: cuser/linker.ld $(OUTDIR)/%.o
	@mkdir -p $(@D)
	$(HUSH_LD) $(LD) $(USER_LDFLAGS) -o $@ -T $^

LIBC_SRCS = ctype.c string.c stdlib.c acpi_strtoul.c

LIBC_OBJS := $(LIBC_SRCS:%.c=$(OUTDIR)/cuser/libc/%.o)
LIBC_OBJS += $(OUTDIR)/cuser/acpica/printf.o
LIBC_OBJS_RAW := $(LIBC_OBJS) $(OUTDIR)/cuser/libc/stdio_raw.o
LIBC_OBJS += $(OUTDIR)/cuser/libc/stdio.o

ALL_ELFS := \
	$(MOD_CFILES:%.c=$(OUTDIR)/%.elf) \
	$(OUTDIR)/cuser/lwip.elf \
	$(OUTDIR)/cuser/acpica.elf
# Anything involved in interrupt processing that could end up affecting the
# console driver has to use raw output for any log printouts to avoid
# deadlocks.
ELFS_WANT_LIBC_RAW := \
	$(OUTDIR)/apic.elf \
	$(OUTDIR)/ioapic.elf \
	$(OUTDIR)/cuser/acpica.elf
ELFS_WANT_LIBC = $(filter-out $(ELFS_WANT_LIBC_RAW), $(ALL_ELFS))

$(ELFS_WANT_LIBC): $(LIBC_OBJS)
$(ELFS_WANT_LIBC_RAW): $(LIBC_OBJS_RAW)

$(GRUB_CFG): build/mkgrubcfg.sh Makefile $(MODFILES)
	@mkdir -p $(@D)
	bash $< $(MOD_ASMFILES:%.asm=%) $(MOD_CFILES:%.c=%) > $@

KERNELS = $(GRUBDIR)/kstart.b $(GRUBDIR)/kcpp
GRUBVER = grub-2.02
GRUB_PREFIX = grub/prefix-$(GRUBVER)
GRUBLIBDIR := $(GRUB_PREFIX)/lib/grub/i386-pc/
GRUB_MKRESCUE = $(GRUB_PREFIX)/bin/grub-mkrescue

# TODO We should change this so that out/grub/ is removed and regenerated each
# build, and put all other output products outside out/grub/
$(OUTDIR)/grub.iso: $(GRUB_CFG) $(KERNELS) $(MODFILES)
	@echo Creating grub boot image $@ from $^
	$(GRUB_MKRESCUE) $(GRUB_MODULES) -d $(GRUBLIBDIR) -o $@ $(GRUBDIR) >/dev/null
	@echo '$@: \\' > $@.d
	@find $(GRUBDIR) | sed 's/$$/ \\/' >> $@.d
	@echo >> $@.d
	@find $(GRUBDIR) | sed 's/$$/:/' >> $@.d

-include $(OUTDIR)/grub.iso.d

ACPICA_SRC =            $(ACPICA)/source
ACPICA_COMMON =         $(ACPICA_SRC)/common
ACPICA_CORE =           $(ACPICA_SRC)/components
ACPICA_INCLUDE =        $(ACPICA_SRC)/include
ACPICA_OSL =            $(ACPICA_SRC)/os_specific/service_layers
ACPICA_TOOLS =          $(ACPICA_SRC)/tools
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
	buffer resrc init ascii resdecode uuid nonansi strtoul64 hex
TB_SRCS := \
	xface xfload instal utils print fadt find xfroot data
EV_SRCS := \
	xface glock xfevnt gpeblk event region handler misc gpe rgnini gpeutil \
	sci xfregn gpeinit
NS_SRCS := \
	xfeval access utils load object walk names eval arguments predef alloc \
	init parse dump search xfname xfobj prepkg repair repair2 convert
EX_SRCS := \
	utils mutex resnte system dump region prep resop resolv convrt create \
	names field store fldio debug oparg1 oparg2 oparg3 oparg6 storen misc \
	config storob concat trace
DB_SRCS := \
	xface input utils histry method names fileio exec disply cmds names stats \
	convert object
DS_SRCS := \
	init wscope wstate opcode wload mthdat object utils field wload2 method \
	wexec args control debug
HW_SRCS := \
	xface acpi gpe pci regs xfsleep esleep sleep valid
PS_SRCS := \
	xface scope utils walk tree opinfo parse opcode args loop object
DM_SRCS := \
	walk utils opcode names buffer deferred resrc resrcs resrcl resrcl2 cstyle
RS_SRCS := \
	xface create dump info list dumpinfo utils calc memory io irq serial misc \
	addr
ACPICA_COMMON_SRCS := \
	ahids.c ahuuids.c

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
	$(ACPICA_COMMON_SRCS:%=$(ACPICA_COMMON)/%)

ACPI_OBJS := $(ACPI_SRCS:$(ACPICA)/%.c=$(ACPICA_OUT)/%.o)
ACPI_OBJS += $(addprefix $(OUTDIR)/cuser/acpica/, \
	acpica.o interrupts.o osl.o malloc.o pci.o)

-include $(ACPI_OBJS:.o=.d)

ACPI_CFLAGS := -Icuser -Icuser/acpica -I$(ACPICA_INCLUDE)
ACPI_CFLAGS += -DACENV_HEADER='"acenv_header.h"'
ACPI_CFLAGS += -DACPI_FULL_DEBUG
ifeq ($(filter clang,$(CC)), clang)
# Triggers a lot on the ACPI_MODULE_NAME construct, when the name is not used.
ACPI_CFLAGS += -Wno-unused-const-variable
endif
ACPI_CFLAGS += -Wno-implicit-fallthrough
# ACPICA doesn't claim to support strict aliasing at all. It has worked fine,
# but it does produce some annoying -Wstrict-aliasing warnings.
ACPI_CFLAGS += -fno-strict-aliasing

$(ACPI_OBJS): USER_CFLAGS += $(ACPI_CFLAGS)

$(OUTDIR)/cuser/acpica.elf: cuser/linker.ld $(ACPI_OBJS)
	@mkdir -p $(@D)
	$(HUSH_LD) $(LD) $(USER_LDFLAGS) -o $@ -T $^

LWIP = lwip
LWIP_CORE = $(LWIP)/src/core
LWIP_NETIF = $(LWIP)/src/netif
LWIP4_CORE = $(LWIP_CORE)/ipv4
LWIP_API = $(LWIP)/src/api

LWIP_NETIF_SRCS = ethernet.c
LWIP_CORE_SRCS = def.c dns.c inet_chksum.c init.c ip.c mem.c memp.c netif.c \
	pbuf.c raw.c stats.c sys.c tcp.c tcp_in.c tcp_out.c timeouts.c udp.c
LWIP4_CORE_SRCS = \
	autoip.c dhcp.c etharp.c icmp.c igmp.c ip4_addr.c ip4.c ip4_frag.c

LWIP_SRCS := \
	$(addprefix $(LWIP_CORE)/, $(LWIP_CORE_SRCS)) \
	$(addprefix $(LWIP_NETIF)/, $(LWIP_NETIF_SRCS)) \
	$(addprefix $(LWIP4_CORE)/, $(LWIP4_CORE_SRCS)) \
	$(addprefix $(LWIP_API)/, api_lib.c api_msg.c err.c netbuf.c netdb.c netifapi.c sockets.c tcpip.c)

LWIP_OBJS := $(LWIP_SRCS:$(LWIP)/%.c=$(LWIP_OUT)/%.o)
LWIP_OBJS += $(addprefix $(OUTDIR)/cuser/, \
	lwip/main.o lwip/http.o)

-include $(LWIP_OBJS:.o=.d)

LWIP_CFLAGS := -Icuser -Icuser/lwip
LWIP_CFLAGS += -I$(LWIP)/src/include
LWIP_CFLAGS += -Wno-parentheses -Wstrict-aliasing -fno-strict-aliasing
ifneq ($(LWIP_DEBUG),YES)
LWIP_CFLAGS += -DNDEBUG
endif

$(LWIP_OBJS): USER_CFLAGS += $(LWIP_CFLAGS)

LWIP_DEP_OBJS := \
	$(LWIP_OBJS) \
	$(LIBC_OBJS)

$(OUTDIR)/cuser/lwip.elf: cuser/linker.ld $(LWIP_DEP_OBJS)
	@mkdir -p $(@D)
	$(HUSH_LD) $(LD) $(USER_LDFLAGS) -o $@ -T $^

yasm/yasm: yasm/Makefile
	$(MAKE) -C yasm

yasm/Makefile: yasm/configure
	cd yasm && ./configure --enable-python

yasm/configure:
	cd yasm && ./autogen.sh

.PHONY: force_kcpp
$(GRUBDIR)/kcpp: out/start32.o force_kcpp
	$(MAKE) -C kcpp

clean::
	$(MAKE) -C kcpp clean
