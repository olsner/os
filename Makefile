# Makefile for Shaaman. Copyright by Simon Brenner, 2002

.PHONY: shaman all clean install commit

BXIMAGE=/opt/bochs/bin/bximage
DD=dd 2>/dev/null

SYSTEM := $(shell uname -s)

YASM ?= yasm/yasm

ifeq ($(VERBOSE),YES)
HUSH_ASM=
HUSH_CC=
HUSH_CXX=
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

ASMFILES := kstart.asm boot.asm
DEPFILES := $(ASMFILES:.asm=.dep)

all: shaman cpuid rflags

shaman: disk.dat

clean:
	rm -f bootfs.img disk.dat
	rm -f boot/boot.b boot/kstart.b boot.lst kstart.lst
	rm -f cpuid rflags
	rm -f $(DEPFILES)

%: %.cpp
	$(HUSH_CXX) $(CXX) $(CXXFLAGS) -o $@ $<

%: %.c
	$(HUSH_CC) $(CC) $(CFLAGS) -o $@ $<

-include $(DEPFILES)

boot/%.b: %.asm
	@mkdir -p $(@D)
	@$(YASM) -e -M $< -o $@ >$*.dep
	$(HUSH_ASM) $(YASM) -f bin $< -o $@ -L nasm -l $*.lst
	@echo ' [ASM]\t'$@: `stat -c %s $@` bytes

bootfs.img: boot/kstart.b
	genromfs -f bootfs.img -d boot -a 512 -x boot.b

disk.dat: partitions.dat boot/boot.b bootfs.img
	@echo Creating disk image from $^
	@$(DD) if=/dev/zero of=$@ bs=10321920 count=1
	@$(DD) if=partitions.dat of=$@ conv=notrunc bs=1 skip=438 seek=438 count=72
	@$(DD) if=boot/boot.b of=$@ conv=notrunc bs=1 count=438
	@$(DD) if=boot/boot.b of=$@ conv=notrunc bs=1 skip=510 seek=510 count=2
	@$(DD) if=bootfs.img of=$@ conv=notrunc bs=512 seek=63
