# TODO Cleaning up of clang/rust/verdigris stuff is probably not done yet.
# It would also be nice to actually port the LTO things to this.

SHELL = /bin/bash

.PHONY: all clean

OUT ?= out
GRUBDIR ?= $(OUT)/grub

AS = $(CLANG) -c
YASM ?= yasm

CXX = g++

CFLAGS = -g -Os -ffunction-sections -fdata-sections
CFLAGS += -fvisibility-inlines-hidden -fvisibility=hidden
CFLAGS += -mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx
CFLAGS += -ffreestanding $(COPTFLAGS)
CFLAGS += -fno-threadsafe-statics
COPTFLAGS = -fno-unroll-loops -funit-at-a-time
CXXFLAGS = $(CFLAGS) -std=gnu++11
LDFLAGS = --check-sections --gc-sections
PUBLIC_SYMBOLS = start64,syscall,irq_entry
OPTFLAGS = -Oz -function-sections -data-sections
OPTFLAGS += -std-link-opts
OPTFLAGS += -internalize-public-api-list=$(PUBLIC_SYMBOLS) -internalize
OPTFLAGS += -argpromotion -mergefunc -deadargelim
RUSTCFLAGS = -g -O --target $(TARGET) --out-dir $(OUT)
RUSTLIBS = -L.

CP = @cp
ifeq ($(VERBOSE),YES)
CP = @cp -v
else
# Why doesn't asmos/Makefile need any -e flags?
HUSH_AS = @echo -e      ' [AS]\t'$@;
HUSH_ASM = @echo -e     ' [ASM]\t'$@;
HUSH_ASM_DEP = @echo    ' [DEP]\t'$@;
HUSH_ASM_DEP = @
HUSH_CC = @echo -e     ' [CC]\t'$@;
HUSH_CXX = @echo -e     ' [CXX]\t'$@;
HUSH_LD  = @echo -e     ' [LD]\t'$@;
HUSH_RUST = @echo -e    ' [RUST]\t'$@;
HUSH_OPT = @echo -e     ' [OPT]\t'$@;
HUSH_LLC = @echo -e    ' [LLC]\t'$@;
hush = @echo -e       ' [$1]\t'$@;
HUSH_DIS=@echo -e     ' [DIS]\t'$@;
endif

all: $(OUT)/kernel $(OUT)/kernel.elf $(OUT)/grub.iso

clean:
	rm -fr out

KERNEL_OBJS = $(addprefix $(OUT)/, runtime.o syscall.o main.o)

KERNEL_OBJS += start32.o

$(OUT)/kernel.elf: linker.ld $(KERNEL_OBJS)
	$(HUSH_LD) $(LD) $(LDFLAGS) --oformat=elf64-x86-64 -o $@ -T $^ -Map $(@:.elf=.map)
	@echo $@: `grep fill $(@:.elf=.map) | tr -s ' ' | cut -d' ' -f4 | while read REPLY; do echo $$[$$REPLY]; done | paste -sd+ | bc` bytes wasted on alignment
$(OUT)/kernel: $(OUT)/kernel.elf
	$(call hush,OBJCOPY) objcopy -O binary $< $@
	@echo $@: `stat -c%s $@` bytes

-include $(OUT)/syscall.d

$(OUT)/%.o: %.s
	@mkdir -p $(@D)
	$(HUSH_AS) as -g -o $@ $<

$(OUT)/%.o: %.cc
	@mkdir -p $(@D)
	$(HUSH_CXX) $(CXX) $(CXXFLAGS) -c -o $@ $<

$(OUT)/%.o: %.asm
	@mkdir -p $(@D)
	$(HUSH_ASM_DEP) $(YASM) -i . -e -M $< -o $@ > $(@:.o=.d)
	$(HUSH_ASM) $(YASM) -i . -f elf64 -g dwarf2 $< -o $@ -L nasm -l $(OUT)/$*.lst

GRUB_MODULES = --modules="boot multiboot"

GRUB_CFG = $(GRUBDIR)/boot/grub/grub.cfg

$(GRUB_CFG): mkgrubcfg.sh
	@mkdir -p $(@D)
	bash $< > $@

$(GRUBDIR)/test.mod: test.asm
	$(HUSH_ASM) $(YASM) -f bin -L nasm -o $@ $<

$(GRUBDIR)/kernel: $(OUT)/kernel
	@$(CP) $< $@

$(OUT)/grub.iso: $(GRUB_CFG) $(GRUBDIR)/kernel $(GRUBDIR)/test.mod
	@echo Creating grub boot image $@ from $^
	grub-mkrescue $(GRUB_MODULES) -o $@ $(GRUBDIR) >/dev/null

