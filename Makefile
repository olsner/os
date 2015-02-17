# Makefile for asmos (SB1?). Copyright by Simon Brenner, 2002-2013

.PHONY: all clean install commit

CP=cp

# gold generates broken modules (and/or the linker script is broken)
LD := ld.bfd
CCACHE ?= ccache
CC := $(CCACHE) gcc
CXX := $(CCACHE) g++
export CC CXX

SYSTEM := $(shell uname -s)

YASM ?= yasm/yasm
ifeq (yasm, $(YASM))
YASMDEP := $(shell which $(YASM))
else
YASMDEP := $(YASM)
endif

ifeq ($(VERBOSE),YES)
CP=cp -v
else
HUSH_ASM=@echo ' [ASM]\t'$@;
#HUSH_ASM_DEP=@echo ' [DEP]\t'$@;
HUSH_ASM_DEP=@
HUSH_CC= @echo ' [CC]\t'$@;
HUSH_CXX=@echo ' [CXX]\t'$@;
HUSH_LD= @echo ' [LD]\t'$@;
HUSH_OBJCOPY= @echo ' [OBJCOPY]\t'$@;
SIZE_ASM=@echo ' [ASM]\t'$@: `stat -c %s $@` bytes
SIZE_LD= @echo ' [LD]\t'$@: `stat -c %s $@` bytes
SIZE_OBJCOPY= @echo ' [OBJCOPY]\t'$@: `stat -c %s $@` bytes
endif

OUTDIR       := out
# Interesting experiment though it currently doesn't work :)
# (The modules are in total about 10kB smaller in X32 mode.)
ifeq ($(X32), YES)
OUTDIR       := out/x32
endif
ACPICA_OUT   := $(OUTDIR)/cuser/acpica
ACPICA       := acpica
LWIP_OUT     := $(OUTDIR)/cuser/lwip
LWIP         := lwip
GRUBDIR      := $(OUTDIR)/grub
MOD_ASMFILES := user/newproc.asm user/gettime.asm user/loop.asm user/shell.asm
MOD_ASMFILES += user/test_puts.asm user/test_xmm.asm
MOD_ASMFILES += kern/console.asm kern/pic.asm kern/irq.asm
ASMFILES     := kstart.asm $(MOD_ASMFILES)
MOD_CFILES   := cuser/helloworld.c cuser/physmem.c cuser/zeropage.c
MOD_CFILES   += cuser/test_maps.c cuser/e1000.c cuser/apic.c cuser/timer_test.c
MOD_CFILES   += cuser/bochsvga.c cuser/fbtest.c cuser/acpi_debugger.c
MOD_CFILES   += cuser/usb/xhci.c
MOD_OFILES   := $(MOD_CFILES:%.c=$(OUTDIR)/%.o)
MOD_ELFS     := $(MOD_CFILES:%.c=$(OUTDIR)/%.elf)
BIG_MODS     := acpica lwip usb
MOD_ELFS     += $(BIG_MODS:%=$(OUTDIR)/cuser/%.elf)

MODFILES     := $(MOD_ASMFILES:%.asm=$(GRUBDIR)/%.mod)
MODFILES     += $(MOD_CFILES:%.c=$(GRUBDIR)/%.mod)
MODFILES     += $(BIG_MODS:%=$(GRUBDIR)/cuser/%.mod)

DEPFILES     := $(ASMFILES:%.asm=$(OUTDIR)/%.d) $(MOD_OFILES:.o=.d)
ASMOUTS      := \
	$(GRUBDIR)/kstart.b \
	$(MODFILES) \
	$(ASMFILES:%.asm=$(OUTDIR)/%.b) \
	$(ASMFILES:.asm=.map) $(ASMFILES:.asm=.lst) \
	$(DEPFILES)

all: cpuid rflags $(OUTDIR)/grub.iso
all: $(MOD_ELFS) $(MODFILES)

.SECONDARY: $(ASMFILES:%.asm=$(OUTDIR)/%.b) $(MOD_OFILES)

clean:
	rm -fr $(OUTDIR)
	rm -f cpuid rflags

%: %.cpp
	$(HUSH_CXX) $(CXX) $(CXXFLAGS) -o $@ $<

%: %.c
	$(HUSH_CC) $(CC) $(CFLAGS) -o $@ $<

# Disable these builtin pattern rules
%.o: %.mod
%: %.mod

-include $(DEPFILES)

$(OUTDIR)/%.d: %.asm $(YASMDEP)
	@mkdir -p $(@D)
	$(HUSH_ASM_DEP) $(YASM) -i . -e -M $< -o $(@:.d=.b) > $(@)

$(OUTDIR)/%.b: %.asm $(OUTDIR)/%.d $(YASMDEP)
	@mkdir -p $(@D)
	$(HUSH_ASM) $(YASM) -i . -f bin $< -o $@ -L nasm -l $(OUTDIR)/$*.lst --mapfile=$(OUTDIR)/$*.map
	$(SIZE_ASM)

-include $(OUTDIR)/start32.d

$(OUTDIR)/start32.o: start32.asm $(YASMDEP)
	@mkdir -p $(@D)
	$(HUSH_ASM_DEP) $(YASM) -i . -e -M $< -o $@ > $(@:.o=.d)
	$(HUSH_ASM) $(YASM) -i . -f elf64 -g dwarf2 $< -o $@ -L nasm -l $(OUTDIR)/start32.lst
	$(SIZE_ASM)

all: $(OUTDIR)/start32.o

$(OUTDIR)/kstart.b: $(INLINE_MODULES)

%.asm.pp: %.asm $(YASMDEP)
	$(YASM) -i . -f bin -o $@ -e -L nasm $<

$(GRUBDIR)/%.b: $(OUTDIR)/%.b
	@mkdir -p $(@D)
	@$(CP) $< $@

$(GRUBDIR)/%.mod: $(OUTDIR)/%.b
	@mkdir -p $(@D)
	@$(CP) $< $@

GRUB_MODULES = --modules="boot multiboot"

GRUB_CFG = $(GRUBDIR)/boot/grub/grub.cfg

USER_CFLAGS := -ffreestanding -g -Os -W -Wall -Wextra -march=native -mno-avx -std=gnu99
USER_CFLAGS += -Wno-unused-function -Wno-unused-parameter
USER_CFLAGS += -ffunction-sections -fdata-sections

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
	$(HUSH_OBJCOPY) objcopy -O binary $< $@
	$(SIZE_OBJCOPY)

$(OUTDIR)/%.elf: cuser/linker.ld $(OUTDIR)/%.o
	@mkdir -p $(@D)
	$(HUSH_LD) $(LD) $(USER_LDFLAGS) -o $@ -T $^

WANT_PRINTF = test_maps zeropage
WANT_PRINTF += timer_test
WANT_REAL_PRINTF = e1000 apic bochsvga fbtest usb/xhci

WANT_STRING = acpica

$(WANT_PRINTF:%=$(OUTDIR)/cuser/%.elf): $(OUTDIR)/cuser/printf.o
$(WANT_REAL_PRINTF:%=$(OUTDIR)/cuser/%.elf): \
	$(OUTDIR)/cuser/acpica/printf.o \
	$(OUTDIR)/cuser/acpica/source/components/utilities/utclib.o

$(WANT_STRING:%=$(OUTDIR)/cuser/%.elf): \
	$(OUTDIR)/cuser/string.o

# TODO This is here because printf.c still depends on AcpiUtStrtoul
$(OUTDIR)/cuser/printf.o: cuser/printf.asm $(YASMDEP)
	@mkdir -p $(@D)
	$(YASM) -f $(YASM_ELF_FORMAT) $< -o $@ -L nasm

$(GRUB_CFG): mkgrubcfg.sh Makefile $(MODFILES)
	@mkdir -p $(@D)
	bash $< $(MOD_ASMFILES:%.asm=%) $(MOD_CFILES:%.c=%) > $@

# TODO We should change this so that out/grub/ is removed and regenerated each
# build, and put all other output products outside out/grub/
$(OUTDIR)/grub.iso: $(GRUB_CFG) $(GRUBDIR)/kstart.b $(MODFILES)
	@echo Creating grub boot image $@ from $^
	grub-mkrescue $(GRUB_MODULES) -o $@ $(GRUBDIR) >/dev/null
	@echo '$@: \\' > $@.d
	@find $(GRUBDIR) | sed 's/$$/ \\/' >> $@.d
	@echo >> $@.d
	@find $(GRUBDIR) | sed 's/$$/:/' >> $@.d

-include $(OUTDIR)/grub.iso.d

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
ACPI_OBJS += $(addprefix $(OUTDIR)/cuser/acpica/, \
	acpica.o osl.o malloc.o pci.o printf.o)

-include $(ACPI_OBJS:.o=.d)

ACPI_CFLAGS := -Icuser -Icuser/acpica -I$(ACPICA_INCLUDE)
ACPI_CFLAGS += -DACENV_HEADER='"acenv_header.h"'
ACPI_CFLAGS += -DACPI_FULL_DEBUG
ifeq ($(filter clang,$(CC)), clang)
# Triggers a lot on the ACPI_MODULE_NAME construct, when the name is not used.
ACPI_CFLAGS += -Wno-unused-const-variable
endif

$(ACPI_OBJS): USER_CFLAGS += $(ACPI_CFLAGS)

$(OUTDIR)/cuser/acpica.elf: cuser/linker.ld $(ACPI_OBJS)
	@mkdir -p $(@D)
	$(HUSH_LD) $(LD) $(USER_LDFLAGS) -o $@ -T $^

LWIP = lwip
LWIP_CORE = $(LWIP)/src/core
LWIP4_CORE = $(LWIP_CORE)/ipv4
LWIP_API = $(LWIP)/src/api

LWIP_CORE_SRCS = def.c dhcp.c dns.c inet_chksum.c init.c mem.c memp.c netif.c \
	pbuf.c raw.c stats.c sys.c tcp.c tcp_in.c tcp_out.c timers.c udp.c
LWIP4_CORE_SRCS = autoip.c icmp.c igmp.c ip4_addr.c ip4.c ip_frag.c

LWIP_SRCS := \
	$(LWIP)/src/netif/etharp.c \
	$(addprefix $(LWIP_CORE)/, $(LWIP_CORE_SRCS)) \
	$(addprefix $(LWIP4_CORE)/, $(LWIP4_CORE_SRCS)) \
	$(addprefix $(LWIP_API)/, api_lib.c api_msg.c err.c netbuf.c netdb.c netifapi.c sockets.c tcpip.c)

LWIP_OBJS := $(LWIP_SRCS:$(LWIP)/%.c=$(LWIP_OUT)/%.o)
LWIP_OBJS += $(addprefix $(OUTDIR)/cuser/, \
	lwip/main.o lwip/http.o)

-include $(LWIP_OBJS:.o=.d)

LWIP_CFLAGS := -Icuser -Icuser/lwip
LWIP_CFLAGS += -I$(LWIP)/src/include
LWIP_CFLAGS += -I$(LWIP)/src/include/ipv4 -I$(LWIP)/src/include/ipv6
LWIP_CFLAGS += -Wno-parentheses -Wstrict-aliasing -fno-strict-aliasing
ifneq ($(LWIP_DEBUG),YES)
LWIP_CFLAGS += -DNDEBUG
endif

$(LWIP_OBJS): USER_CFLAGS += $(LWIP_CFLAGS)

LWIP_DEP_OBJS := $(LWIP_OBJS) $(OUTDIR)/cuser/acpica/printf.o $(OUTDIR)/cuser/acpica/source/components/utilities/utclib.o

$(OUTDIR)/cuser/lwip.elf: cuser/linker.ld $(LWIP_DEP_OBJS)
	@mkdir -p $(@D)
	$(HUSH_LD) $(LD) $(USER_LDFLAGS) -o $@ -T $^

USB_SRCS := $(USB)/main.c
USB_OBJS := $(USB_SRCS:%.c:$(OUTDIR)/%.o)

-include $(USB_OBJS:.o=.d)

USB_DEP_OBJS := $(USB_OBJS) $(OUTDIR)/cuser/acpica/printf.o $(OUTDIR)/cuser/acpica/source/components/utilities/utclib.o

$(OUTDIR)/cuser/usb.elf: cuser/linker.ld $(USB_DEP_OBJS)
	@mkdir -p $(@D)
	$(HUSH_LD) $(LD) $(USER_LDFLAGS) -o $@ -T $^

yasm/yasm: yasm/Makefile
	$(MAKE) -C yasm

yasm/Makefile: yasm/configure
	cd yasm && ./configure --enable-python

yasm/configure:
	cd yasm && ./autogen.sh
