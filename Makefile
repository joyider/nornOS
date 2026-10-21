# Build the nornOS kernel together with one application from applications/.
#
#   make APP=tc01            build ELF/HEX for the board named in applications/tc01/app.mk
#   make APP=tc01 BOARD=uno  override the board from the command line
#   make APP=tc01 sim        run in simavr (UART0 output goes to the terminal)
#   make APP=tc01 upload     flash with avrdude (set PORT=/dev/ttyACM0)
#   make host-test           build and run the kernel unit tests on the PC
#   make clean
#
# Boards: mega2560 (ATmega2560, Arduino Mega) and uno (ATmega328P, Arduino UNO/Nano).
# The application selects its default board in app.mk (BOARD = uno); a BOARD given on the
# command line wins. The C code sees the board through the MCU macros (hal/board.h).

APP     ?= tc01
APPDIR   = applications/$(APP)
-include $(APPDIR)/app.mk
BOARD   ?= mega2560
PORT    ?= /dev/ttyACM0

ifeq ($(BOARD),mega2560)
  MCU      = atmega2560
  F_CPU    = 16000000UL
  AVRDUDE_PROG = -c wiring -b 115200 -D
else ifeq ($(BOARD),uno)
  MCU      = atmega328p
  F_CPU    = 16000000UL
  AVRDUDE_PROG = -c arduino -b 115200
else
  $(error Unknown BOARD '$(BOARD)': use mega2560 or uno)
endif

CC       = avr-gcc
OBJCOPY  = avr-objcopy
SIZE     = avr-size
HOSTCC   = gcc

BUILD    = build/$(APP)-$(BOARD)
TARGET   = $(BUILD)/rtos_$(APP)

KERNEL_SRC = $(wildcard kernel/*.c)
PORT_SRC   = $(wildcard port/avr/*.c)
PORT_ASM   = $(wildcard port/avr/*.S)
HAL_SRC    = $(wildcard hal/*.c)
APP_SRC    = $(wildcard $(APPDIR)/*.c)

OBJ  = $(patsubst %.c,$(BUILD)/%.o,$(KERNEL_SRC) $(PORT_SRC) $(HAL_SRC))
OBJ += $(patsubst %.S,$(BUILD)/%.o,$(PORT_ASM))
OBJ += $(patsubst $(APPDIR)/%.c,$(BUILD)/app/%.o,$(APP_SRC))

INC     = -Ikernel -Iport/avr -Ihal -I$(APPDIR)
CFLAGS  = -std=c99 -pedantic -Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes \
          -Os -g -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(INC) \
          -ffunction-sections -fdata-sections -fno-common -MMD -MP
ASFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(INC) -x assembler-with-cpp -g -MMD -MP
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections,--relax -Wl,-Map,$(TARGET).map

.PHONY: all sim upload host-test clean size

all: $(TARGET).hex size

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: $(TARGET).elf
	$(SIZE) --format=avr --mcu=$(MCU) $<

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(BUILD)/app/%.o: $(APPDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

sim: $(TARGET).elf
	simavr -m $(MCU) -f $(F_CPU:UL=) $<

upload: $(TARGET).hex
	avrdude -p $(MCU) $(AVRDUDE_PROG) -P $(PORT) -U flash:w:$<:i

# ---- host unit tests --------------------------------------------------------------------
# Every test/test_*.c is one test program; all share test/rtos_config.h.
HOST_BUILD = build/host
HOST_KERNEL = $(KERNEL_SRC) $(wildcard port/host/*.c)
HOST_TESTS = $(patsubst test/%.c,$(HOST_BUILD)/%,$(wildcard test/test_*.c))
HOST_FLAGS = -std=c99 -pedantic -Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes -g -O0 \
             -Ikernel -Iport/host -Itest

$(HOST_BUILD)/%: test/%.c $(HOST_KERNEL) $(wildcard kernel/*.h) $(wildcard port/host/*.h) test/rtos_config.h
	@mkdir -p $(HOST_BUILD)
	$(HOSTCC) $(HOST_FLAGS) -o $@ $< $(HOST_KERNEL)

host-test: $(HOST_TESTS)
	@for t in $(HOST_TESTS); do echo "== $$t"; $$t | tail -1 || exit 1; done

clean:
	rm -rf build

-include $(OBJ:.o=.d)
