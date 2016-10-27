SHELL = /bin/bash

.PHONY: all clean

OUT ?= out
GRUBDIR ?= ../out/grub

YASM ?= ../yasm/yasm

CFLAGS = -g -Os -ffunction-sections -fdata-sections
CFLAGS += -fvisibility-inlines-hidden -fvisibility=hidden
CFLAGS += -mcmodel=kernel -mno-red-zone -mno-sse -mno-mmx
CFLAGS += -ffreestanding $(COPTFLAGS)
CFLAGS += -fno-threadsafe-statics
CFLAGS += -Wall -Wextra -Werror -Wno-error=unused-parameter
COPTFLAGS = -fno-unroll-loops -funit-at-a-time
CXXFLAGS = $(CFLAGS) -std=gnu++14 -fno-exceptions -fno-rtti
LDFLAGS = --check-sections --gc-sections

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
hush = @echo -e       ' [$1]\t'$@;
endif

all: $(GRUBDIR)/kcpp

clean:
	rm -fr out

KERNEL_OBJS = $(addprefix $(OUT)/, runtime.o syscall.o main.o)

KERNEL_OBJS += start32.o

KERNEL_DEPS = $(KERNEL_OBJS:.o=.d)
-include $(KERNEL_DEPS)

$(OUT)/kernel.elf: linker.ld $(KERNEL_OBJS)
	$(HUSH_LD) $(LD) $(LDFLAGS) --oformat=elf64-x86-64 -o $@ -T $^ -Map $(@:.elf=.map)
	@echo $@: `grep fill $(@:.elf=.map) | tr -s ' ' | cut -d' ' -f4 | while read REPLY; do echo $$[$$REPLY]; done | paste -sd+ | bc` bytes wasted on alignment
$(OUT)/kernel: $(OUT)/kernel.elf
	$(call hush,OBJCOPY) objcopy -O binary $< $@
	@echo $@: `stat -c%s $@` bytes

-include $(OUT)/syscall.d

$(OUT)/%.o: %.s
	@mkdir -p $(@D)
	$(HUSH_AS) $(AS) -g -o $@ $<

$(OUT)/%.o: %.cc
	@mkdir -p $(@D)
	$(HUSH_CXX) $(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

$(OUT)/%.o: %.asm
	@mkdir -p $(@D)
	$(HUSH_ASM_DEP) $(YASM) -i . -e -M $< -o $@ > $(@:.o=.d)
	$(HUSH_ASM) $(YASM) -i . -f elf64 -g dwarf2 $< -o $@ -L nasm -l $(OUT)/$*.lst

$(GRUBDIR)/kcpp: $(OUT)/kernel
	@$(CP) $< $@

