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

ifeq ($(SYSTEM), Darwin)
BUILD_OBJ ?= macho64
SYMBOLPREFIX ?= --prefix _
else
BUILD_OBJ ?= elf64
endif

ASMFILES := kstart.asm
DEPFILES := $(ASMFILES:.asm=.dep)
ASMOUTS  := $(patsubst %.asm, grub/%.b, $(ASMFILES)) \
			$(ASMFILES:.asm=.map) $(ASMFILES:.asm=.lst) \
			$(DEPFILES)

all: cpuid rflags grub.iso

clean:
	rm -f grub.iso
	rm -f $(ASMOUTS)
	rm -f cpuid rflags
	rm -f $(DEPFILES)

%: %.cpp
	$(HUSH_CXX) $(CXX) $(CXXFLAGS) -o $@ $<

%: %.c
	$(HUSH_CC) $(CC) $(CFLAGS) -o $@ $<

-include $(DEPFILES)

grub/%.b: %.asm
	@mkdir -p $(@D)
	@$(YASM) -e -M $< -o $@ >$*.dep
	$(HUSH_ASM) $(YASM) -f bin $< -o $@ -L nasm -l $*.lst
	@echo ' [ASM]\t'$@: `stat -c %s $@` bytes

GRUB_MODULES = --modules="boot multiboot"

grub.iso: grub/kstart.b grub/boot/grub/grub.cfg
	@echo Creating grub boot image $@ from $^
	grub-mkrescue --diet $(GRUB_MODULES) -o $@ grub/ >/dev/null
