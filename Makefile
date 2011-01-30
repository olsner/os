# Makefile for Shaaman. Copyright by Simon Brenner, 2002

.PHONY: shaman all clean install commit

BXIMAGE=/opt/bochs/bin/bximage
DD=dd 2>/dev/null

SYSTEM := $(shell uname -s)

ifeq ($(VERBOSE),YES)
HUSH_ASM=
HUSH_CC=
HUSH_CXX=
else
HUSH_ASM=@echo ' [NASM]\t'$@;
HUSH_CC=@echo ' [CC]\t'$@;
HUSH_CXX=@echo ' [CXX]\t'$@;
endif

ifeq ($(SYSTEM), Darwin)
BUILD_OBJ ?= macho64
SYMBOLPREFIX ?= --prefix _
else
BUILD_OBJ ?= elf64
endif

INCFILES := $(wildcard *.inc)

all: shaman cpuid rflags retbench

shaman: disk.dat

clean:
	rm -f bootfs.img disk.dat
	rm -f boot/boot.b boot/kstart.b boot.lst kstart.lst
	rm -f cpuid rflags

%: %.cpp
	$(HUSH_CXX) $(CXX) $(CXXFLAGS) -o $@ $<

%: %.c
	$(HUSH_CC) $(CC) $(CFLAGS) -o $@ $<

%: %.asm
	$(HUSH_ASM) nasm -w+all -Ox -f $(BUILD_OBJ) $(SYMBOLPREFIX) -o $*.o $<
	$(HUSH_CC) $(CC) -o $@ $*.o

# TODO Proper dependency detection. nasm does do it.
%.asm: $(INCFILES)

boot/%.b: %.asm
	@mkdir -p $(@D)
	$(HUSH_ASM) nasm -w+all -Ox -f bin $< -o $@ -l $*.lst

bootfs.img: boot/kstart.b
	genromfs -f bootfs.img -d boot -a 512 -x boot.b

disk.dat: partitions.dat boot/boot.b bootfs.img
	@echo Creating disk image from $^
	@$(DD) if=/dev/zero of=$@ bs=10321920 count=1
	@$(DD) if=partitions.dat of=$@ conv=notrunc bs=1 skip=438 seek=438 count=72
	@$(DD) if=boot/boot.b of=$@ conv=notrunc bs=1 count=438
	@$(DD) if=boot/boot.b of=$@ conv=notrunc bs=1 skip=510 seek=510 count=2
	@$(DD) if=bootfs.img of=$@ conv=notrunc bs=512 seek=63
