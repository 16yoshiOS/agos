# =============================================================================
#  AGOS - Makefile (GRUB / Multiboot edition)
# -----------------------------------------------------------------------------
#  Pipeline:
#    boot/start.asm, kernel/*.asm --nasm (elf32)--> build/**/*.o
#    kernel/*.c                   --gcc -m32----->  build/kernel/*.o
#    all objects + linker.ld      --ld----------->  build/kernel.elf   (Multiboot)
#    kernel.elf + boot/grub/grub.cfg --grub-mkrescue--> build/agos.iso (bootable)
#
#  Targets
#    make            build + verify build/kernel.elf
#    make iso        build the bootable ISO  -> build/agos.iso
#    make run        boot the ISO in QEMU (serial log on this terminal)
#    make run-kernel boot kernel.elf directly in QEMU (no GRUB; text fallback)
#    make test       run the host-side unit tests
#    make size       section sizes
#    make clean
#
#  Packages (Debian/Ubuntu):
#    build the kernel:  nasm gcc binutils make python3
#    build the ISO:     grub-pc-bin grub-common xorriso mtools
#    run:               qemu-system-x86
# =============================================================================

AS      := nasm
CC      := gcc
LD      := ld
HOSTCC  := gcc
PYTHON  := python3
QEMU    := qemu-system-i386
GRUB_MKRESCUE ?= grub-mkrescue

BUILD      := build
KERNEL_ELF := $(BUILD)/kernel.elf
ISO        := $(BUILD)/agos.iso
ISO_DIR    := $(BUILD)/iso
LINKER_LD  := kernel/linker.ld

SRC_DIRS := kernel kernel/drivers kernel/gui kernel/mm kernel/sys kernel/apps kernel/browser
C_SRCS   := $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.c))
ASM_SRCS := boot/start.asm $(foreach d,$(SRC_DIRS),$(wildcard $(d)/*.asm))
INCLUDES := $(addprefix -I,$(SRC_DIRS))
OBJS     := $(patsubst %.c,$(BUILD)/%.o,$(C_SRCS)) \
            $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SRCS))
DEPS     := $(patsubst %.c,$(BUILD)/%.d,$(C_SRCS))

# --- Compiler flags ----------------------------------------------------------
#  -m32 -march=i486 -mtune=i686                    32-bit x86 code
#  -ffreestanding                      no hosted libc assumptions
#  -fno-pic -fno-pie                   absolute addressing, fixed link address
#  -fno-stack-protector                no __stack_chk_fail dependency
#  -fno-asynchronous-unwind-tables     no .eh_frame
#  -fcf-protection=none                no endbr32 (distro gcc may default it on)
#  -mgeneral-regs-only                 never touch SSE/MMX/x87 (FPU not set up)
#  -mpreferred-stack-boundary=2        4-byte stack alignment: interrupt entry
#                                      cannot guarantee 16-byte alignment
#  -fno-tree-loop-distribute-patterns  never turn loops into memset/memcpy calls
CFLAGS := -m32 -march=i486 -mtune=i686 -std=gnu11 -O2 -g0 \
          -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -fno-asynchronous-unwind-tables -fcf-protection=none \
          -mgeneral-regs-only -mpreferred-stack-boundary=2 \
          -fno-tree-loop-distribute-patterns \
          -Wall -Wextra -Wshadow -Wno-unused-parameter \
          $(INCLUDES) -MMD -MP

ASFLAGS := -f elf32 -Wall

# 4 KiB max page size keeps the ELF compact (no 2 MiB segment padding) and
# puts the Multiboot header well inside the first 8 KiB of the file.
# --no-warn-rwx-segments exists only in binutils >= 2.39; probe for it.
LD_NO_RWX_WARN := $(shell $(LD) --help 2>/dev/null | grep -q -- --no-warn-rwx-segments && echo --no-warn-rwx-segments)
LDFLAGS := -m elf_i386 -nostdlib -z max-page-size=0x1000 -z noexecstack \
           $(LD_NO_RWX_WARN) -T $(LINKER_LD)

# =============================================================================
.PHONY: all iso run run-kernel test size check clean

all: check

# --- Compile -----------------------------------------------------------------
$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# --- Link --------------------------------------------------------------------
$(KERNEL_ELF): $(OBJS) $(LINKER_LD)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Verify the Multiboot header / ELF layout (and ask grub-file if installed).
check: $(KERNEL_ELF)
	@$(PYTHON) tools/check_kernel.py $(KERNEL_ELF)
	@if command -v grub-file >/dev/null 2>&1; then \
	    grub-file --is-x86-multiboot $(KERNEL_ELF) && echo "  OK      grub-file: valid Multiboot kernel" \
	      || { echo "grub-file rejected $(KERNEL_ELF)"; exit 1; }; \
	fi

# --- ISO ---------------------------------------------------------------------
iso: $(ISO)

$(ISO): $(KERNEL_ELF) boot/grub/grub.cfg
	@command -v $(GRUB_MKRESCUE) >/dev/null 2>&1 || { \
	    echo "error: $(GRUB_MKRESCUE) not found."; \
	    echo "       apt install grub-pc-bin grub-common xorriso mtools"; exit 1; }
	@$(MAKE) --no-print-directory check
	rm -rf $(ISO_DIR)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/kernel.elf
	cp boot/grub/grub.cfg $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR)
	@echo "  ISO     $@ ($$(stat -c %s $@) bytes)"

# --- Run (QEMU) ----------------------------------------------------------------
run: $(ISO)
	$(QEMU) -cdrom $(ISO) -m 128 -serial stdio

# QEMU's built-in Multiboot loader: no GRUB, no framebuffer (VGA text fallback).
run-kernel: $(KERNEL_ELF)
	$(QEMU) -kernel $(KERNEL_ELF) -m 128 -serial stdio

# --- Host-side unit tests --------------------------------------------------------
# Pure logic (lib, GDT/IDT encoders, scancode decoder, PIT divisor) is compiled
# with the *host* compiler. lib.c's mem*/str* are renamed so they do not clash
# with the host libc.
TEST_RENAMES := -Dmemset=k_memset -Dmemcpy=k_memcpy -Dmemmove=k_memmove \
                -Dmemcmp=k_memcmp -Dstrlen=k_strlen
test: $(BUILD)/host_tests $(BUILD)/sim_tests $(BUILD)/sys_tests
	./$(BUILD)/host_tests
	./$(BUILD)/sim_tests
	./$(BUILD)/sys_tests

$(BUILD)/host_tests: tests/host_tests.c kernel/lib.c kernel/gdt.c kernel/idt.c \
                     kernel/drivers/kbd_decode.c kernel/drivers/mouse_decode.c \
                     kernel/gui/font.c $(wildcard kernel/*.h)
	@mkdir -p $(BUILD)
	$(HOSTCC) -std=gnu11 -O1 -g -fno-builtin -Wall -Wextra -Wno-unused-parameter \
	    $(INCLUDES) $(TEST_RENAMES) -o $@ tests/host_tests.c kernel/lib.c \
	    kernel/gdt.c kernel/idt.c kernel/drivers/kbd_decode.c \
	    kernel/drivers/mouse_decode.c kernel/gui/font.c

# Hardware simulation: the real isr/pic/pit/keyboard/mouse/ps2 code against a
# software model of the I/O ports. -DAGOS_IO_H switches off kernel/io.h;
# tests/fake_io.h is force-included in its place.
SIM_SRCS := kernel/isr.c kernel/drivers/pic.c kernel/drivers/pit.c \
            kernel/drivers/keyboard.c kernel/drivers/kbd_decode.c \
            kernel/drivers/mouse.c kernel/drivers/mouse_decode.c kernel/drivers/ps2.c
$(BUILD)/sim_tests: tests/sim_tests.c tests/fake_io.c tests/fake_io.h $(SIM_SRCS) \
                    $(wildcard kernel/*.h kernel/*/*.h)
	@mkdir -p $(BUILD)
	$(HOSTCC) -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-pedantic \
	    $(INCLUDES) -Itests -DAGOS_IO_H -include tests/fake_io.h -o $@ \
	    tests/sim_tests.c tests/fake_io.c $(SIM_SRCS)

# Memory manager, accounting, CPU and RTC on a malloc'ed "physical memory" arena.
SYS_SRCS := kernel/mm/pmm.c kernel/mm/heap.c kernel/sys/acct.c kernel/sys/cpu.c \
            kernel/sys/rtc.c kernel/lib.c
$(BUILD)/sys_tests: tests/sys_tests.c tests/fake_io.c tests/fake_io.h $(SYS_SRCS) \
                    $(wildcard kernel/*.h kernel/*/*.h)
	@mkdir -p $(BUILD)
	$(HOSTCC) -std=gnu11 -O1 -g -fno-builtin -Wall -Wextra -Wno-unused-parameter \
	    $(INCLUDES) -Itests -DAGOS_IO_H -include tests/fake_io.h $(TEST_RENAMES) -o $@ \
	    tests/sys_tests.c tests/fake_io.c $(SYS_SRCS)

# --- Housekeeping ----------------------------------------------------------------
size: $(KERNEL_ELF)
	size $(KERNEL_ELF)

clean:
	rm -rf $(BUILD)

-include $(DEPS)
