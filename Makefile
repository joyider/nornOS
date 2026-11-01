# nornOS: build the kernel together with one application from applications/.
#
#   make APP=tc01            build for the board named in applications/tc01/app.mk
#   make APP=tc01 BOARD=uno  override the board from the command line
#   make APP=tc01 sim        run in a simulator (simavr for AVR); UART0 goes to the terminal
#   make APP=tc01 upload     flash the board (PORT=/dev/ttyACM0 or /dev/ttyUSB0)
#   make host-test           build and run the kernel unit tests on the PC
#   make clean
#
# A board belongs to an architecture; the architecture's port/<arch>/arch.mk supplies the
# toolchain, flags and the image/sim/upload rules. The C code never sees BOARD: it is
# compiled for the MCU, and hal/<arch>/board.h derives the pin map from the MCU macros.

APP     ?= tc01
APPDIR   = applications/$(APP)
-include $(APPDIR)/app.mk
BOARD   ?= mega2560

ARCH_mega2560 = avr
ARCH_uno      = avr
ARCH_esp32    = xtensa
ARCH = $(ARCH_$(BOARD))
ifeq ($(ARCH),)
  $(error Unknown BOARD '$(BOARD)'; known: $(patsubst ARCH_%,%,$(filter ARCH_%,$(.VARIABLES))))
endif

# EXTRA_CFLAGS adds compiler flags from the command line, e.g. -DRTOS_CFG_X=1 to try a
# configuration without editing the application; such builds get their own directory.
BUILD    = build/$(APP)-$(BOARD)$(if $(EXTRA_CFLAGS),-extra)
TARGET   = $(BUILD)/nornos_$(APP)

KERNEL_SRC = $(wildcard kernel/*.c)
PORT_SRC   = $(wildcard port/$(ARCH)/*.c)
PORT_ASM   = $(wildcard port/$(ARCH)/*.S)
HAL_SRC    = $(wildcard hal/*.c) $(wildcard hal/$(ARCH)/*.c)
APP_SRC    = $(wildcard $(APPDIR)/*.c)

OBJ  = $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_SRC) $(PORT_SRC) $(HAL_SRC))
OBJ += $(patsubst %.S,$(BUILD)/%.o,$(PORT_ASM))
OBJ += $(patsubst $(APPDIR)/%.c,$(BUILD)/app/%.o,$(APP_SRC))

INC = -Ikernel -Iport/$(ARCH) -Ihal -Ihal/$(ARCH) -I$(APPDIR)

.PHONY: all image sim upload monitor host-test test-all clean

all: image

include port/$(ARCH)/arch.mk
CFLAGS  += $(EXTRA_CFLAGS)

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(BUILD)/app/%.o: $(APPDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- host unit tests --------------------------------------------------------------------
# Every test/test_*.c is one test program; all share test/rtos_config.h.
HOSTCC      = gcc
HOST_BUILD  = build/host
HOST_KERNEL = $(KERNEL_SRC) $(wildcard port/host/*.c)
HOST_TESTS  = $(patsubst test/%.c,$(HOST_BUILD)/%,$(wildcard test/test_*.c))
# A test with its own kernel configuration keeps it in test/<name>/rtos_config.h, which
# takes precedence over test/rtos_config.h. Sanitizers are on; SANITIZE= turns them off.
SANITIZE   ?= -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
HOST_FLAGS  = -std=c99 -pedantic -Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes -Wconversion \
              -g -O0 $(SANITIZE) -Ikernel -Iport/host

$(HOST_BUILD)/%: test/%.c $(HOST_KERNEL) $(wildcard kernel/*.h) $(wildcard port/host/*.h) $(wildcard test/rtos_config.h test/*/rtos_config.h)
	@mkdir -p $(HOST_BUILD)
	$(HOSTCC) $(HOST_FLAGS) -Itest/$* -Itest -o $@ $< $(HOST_KERNEL)

host-test: $(HOST_TESTS)
	@for t in $(HOST_TESTS); do echo "== $$t"; ASAN_OPTIONS=detect_stack_use_after_return=0 $$t | tail -1 || exit 1; done

# Everything: host tests, every tc application in simavr for the AVR boards and on an
# ESP32 if one is connected. See tools/test_all.sh for the variables it takes.
test-all:
	tools/test_all.sh

clean:
	rm -rf build

-include $(OBJ:.o=.d)
