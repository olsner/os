# Makefile for asmos (SB1?). Copyright by Simon Brenner, 2002-2013

.PHONY: all clean install commit

BXIMAGE=/opt/bochs/bin/bximage
DD=dd 2>/dev/null
CP=cp

SYSTEM := $(shell uname -s)

YASM ?= yasm/yasm

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

OUTDIR       := out
GRUBDIR      := $(OUTDIR)/grub
MOD_ASMFILES := user/newproc.asm user/gettime.asm user/loop.asm user/shell.asm
MOD_ASMFILES += user/test_puts.asm user/test_xmm.asm
MOD_ASMFILES += kern/console.asm kern/pic.asm kern/irq.asm
ASMFILES     := kstart.asm $(MOD_ASMFILES)
MOD_CFILES   := cuser/helloworld.c cuser/physmem.c cuser/zeropage.c
MOD_OFILES   := $(MOD_CFILES:%.c=$(OUTDIR)/%.o)
MODFILES     := $(MOD_ASMFILES:%.asm=$(GRUBDIR)/%.mod) $(MOD_CFILES:%.c=$(GRUBDIR)/%.mod)
DEPFILES     := $(ASMFILES:%.asm=$(OUTDIR)/%.d) $(MOD_OFILES:.o=.d)
ASMOUTS      := \
	$(GRUBDIR)/kstart.b \
	$(MODFILES) \
	$(ASMFILES:%.asm=$(OUTDIR)/%.b) \
	$(ASMFILES:.asm=.map) $(ASMFILES:.asm=.lst) \
	$(DEPFILES)

all: cpuid rflags $(OUTDIR)/grub.iso

.SECONDARY: $(ASMFILES:%.asm=$(OUTDIR)/%.b) $(MOD_OFILES)

clean:
	rm -fr $(OUTDIR)
	rm -f cpuid rflags

%: %.cpp
	$(HUSH_CXX) $(CXX) $(CXXFLAGS) -o $@ $<

%: %.c
	$(HUSH_CC) $(CC) $(CFLAGS) -o $@ $<

-include $(DEPFILES)

$(OUTDIR)/%.d: %.asm
	@mkdir -p $(@D)
	$(HUSH_ASM_DEP) $(YASM) -i . -e -M $< -o $@ > $(@:.b=.d)

$(OUTDIR)/%.b: %.asm $(OUTDIR)/%.d
	@mkdir -p $(@D)
	$(HUSH_ASM) $(YASM) -i . -f bin $< -o $@ -L nasm -l $*.lst
	@echo ' [ASM]\t'$@: `stat -c %s $@` bytes

$(OUTDIR)/kstart.b: $(INLINE_MODULES)

%.asm.pp: %.asm
	$(YASM) -i . -f bin -o $@ -e -L nasm $<

$(GRUBDIR)/%.b: $(OUTDIR)/%.b
	@mkdir -p $(@D)
	@$(CP) $< $@

$(GRUBDIR)/%.mod: $(OUTDIR)/%.b
	@mkdir -p $(@D)
	@$(CP) $< $@

GRUB_MODULES = --modules="boot multiboot"

GRUB_CFG = $(GRUBDIR)/boot/grub/grub.cfg

USER_CFLAGS = -ffreestanding -Os -W -Wall -Wextra -march=native
USER_CFLAGS += -Wno-unused-function

$(OUTDIR)/cuser/%.o: cuser/%.c
	@mkdir -p $(@D)
	$(HUSH_CC) $(CC) $(USER_CFLAGS) -c -MP -MMD -o $@ $<

$(GRUBDIR)/%.mod: cuser/linker.ld $(OUTDIR)/%.o
	@mkdir -p $(@D)
	$(HUSH_LD) $(LD) -o $@ -T $^ -Map $(OUTDIR)/$*.map

$(GRUB_CFG): mkgrubcfg.sh Makefile $(MODFILES)
	@mkdir -p $(@D)
	bash $< $(MOD_ASMFILES:%.asm=%) $(MOD_CFILES:%.c=%) > $@

# TODO We should change this so that out/grub/ is removed and regenerated each
# build, and put all other output products outside out/grub/
$(OUTDIR)/grub.iso: $(GRUB_CFG) $(GRUBDIR)/kstart.b $(MODFILES)
	@echo Creating grub boot image $@ from $^
	grub-mkrescue $(GRUB_MODULES) -o $@ $(GRUBDIR) >/dev/null
