# Makefile for Shaaman. Copyright by Simon Brenner, 2002

.PHONY: all clean install commit

BXIMAGE=/opt/bochs/bin/bximage
DD=dd 2>/dev/null
CP=cp

SYSTEM := $(shell uname -s)

YASM ?= yasm/yasm

ifeq ($(VERBOSE),YES)
HUSH_ASM=
HUSH_CC=
HUSH_CXX=
CP=cp -v
else
HUSH_ASM=@echo ' [ASM]\t'$@;
HUSH_CC=@echo ' [CC]\t'$@;
HUSH_CXX=@echo ' [CXX]\t'$@;
endif

OUTDIR   := out
GRUBDIR  := $(OUTDIR)/grub
MOD_ASMFILES := user/newproc.asm user/gettime.asm user/loop.asm
MOD_ASMFILES += user/test_puts.asm user/test_xmm.asm
MOD_ASMFILES += kern/console.asm kern/pic.asm kern/irq.asm
ASMFILES := kstart.asm $(MOD_ASMFILES)
MODFILES := $(MOD_ASMFILES:%.asm=$(GRUBDIR)/%.mod)
DEPFILES := $(ASMFILES:.asm=.dep)
ASMOUTS  := \
	$(GRUBDIR)/kstart.b \
	$(MODFILES) \
	$(ASMFILES:%.asm=$(OUTDIR)/%.b) \
	$(ASMFILES:.asm=.map) $(ASMFILES:.asm=.lst) \
	$(DEPFILES)
INLINE_MODULES = $(OUTDIR)/kern/irq.b

all: cpuid rflags $(OUTDIR)/grub.iso

.SECONDARY: $(ASMFILES:%.asm=$(OUTDIR)/%.b)

clean:
	rm -f $(OUTDIR)/grub.iso
	rm -f cpuid rflags
	rm -f $(ASMOUTS)
	rm -f $(DEPFILES)

%: %.cpp
	$(HUSH_CXX) $(CXX) $(CXXFLAGS) -o $@ $<

%: %.c
	$(HUSH_CC) $(CC) $(CFLAGS) -o $@ $<

-include $(DEPFILES)

$(OUTDIR)/%.b: %.asm
	@mkdir -p $(@D)
	@$(YASM) -i . -e -M $< -o $@ >$*.dep
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

$(GRUB_CFG): mkgrubcfg.sh Makefile $(MODFILES)
	@mkdir -p $(@D)
	bash $< $(MOD_ASMFILES:%.asm=%) > $@

# TODO We should change this so that out/grub/ is removed and regenerated each
# build, and put all other output products outside out/grub/
$(OUTDIR)/grub.iso: $(GRUB_CFG) $(GRUBDIR)/kstart.b $(MODFILES)
	@echo Creating grub boot image $@ from $^
	grub-mkrescue $(GRUB_MODULES) -o $@ $(GRUBDIR) >/dev/null
