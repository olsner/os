# NB! This make file depends on cross-toolchain setup in toplevel makefile, so
# it's not actually independently buildable anymore.

include ../build/common.mk
include ../build/makejobs.mk

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
CXXFLAGS = $(CFLAGS) -std=gnu++14
LDFLAGS = --check-sections --gc-sections

HOST_CXXFLAGS = -g -Os -std=gnu++14

all: $(GRUBDIR)/kcpp

clean:
	rm -fr out

KERNEL_OBJS = $(addprefix $(OUT)/, runtime.o syscall.o main.o)

KERNEL_OBJS += start32.o

KERNEL_DEPS = $(KERNEL_OBJS:.o=.d)
-include $(KERNEL_DEPS)

$(OUT)/kernel.elf: linker.ld $(KERNEL_OBJS)
	$(HUSH_LD) $(LD) $(LDFLAGS) --oformat=elf64-x86-64 -o $@ -T $^ -Map $(@:.elf=.map)
	@echo $@: `awk '/fill/ { sum += $$3 } END { print sum } ' <$(@:.elf=.map)` bytes wasted on alignment
$(OUT)/kernel: $(OUT)/kernel.elf
	$(HUSH_OBJCOPY) $(OBJCOPY) -O binary $< $@
	@echo $@: `$(FILE_SIZE) $@` bytes

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

$(OUT)/xprintf_test: xprintf.cpp
	$(HUSH_CXX) $(HOST_CXX) $(HOST_CXXFLAGS) -o $@ $< -DXPRINTF_TEST

test-xprintf: $(OUT)/xprintf_test
	@$<

all: test-xprintf
