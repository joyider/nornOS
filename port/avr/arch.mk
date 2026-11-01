# AVR architecture: boards, toolchain and image rules. Included by the top Makefile after
# BOARD, BUILD, TARGET and INC are known. Must define: CC, CFLAGS, ASFLAGS, LDFLAGS and the
# targets image, sim and upload.

ifeq ($(BOARD),mega2560)
  MCU          = atmega2560
  F_CPU        = 16000000UL
  AVRDUDE_PROG = -c wiring -b 115200 -D
else ifeq ($(BOARD),uno)
  MCU          = atmega328p
  F_CPU        = 16000000UL
  AVRDUDE_PROG = -c arduino -b 115200
else
  $(error port/avr/arch.mk does not know BOARD '$(BOARD)')
endif

PORT   ?= /dev/ttyACM0

CC      = avr-gcc
OBJCOPY = avr-objcopy
SIZE    = avr-size

CFLAGS  = -std=c99 -pedantic -Wall -Wextra -Wshadow -Wundef -Wstrict-prototypes \
          -Os -g -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(INC) \
          -ffunction-sections -fdata-sections -fno-common -MMD -MP
ASFLAGS = -mmcu=$(MCU) -DF_CPU=$(F_CPU) $(INC) -x assembler-with-cpp -g -MMD -MP
LDFLAGS = -mmcu=$(MCU) -Wl,--gc-sections,--relax -Wl,-Map,$(TARGET).map

image: $(TARGET).hex
	$(SIZE) --format=avr --mcu=$(MCU) $(TARGET).elf

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

sim: $(TARGET).elf
	simavr -m $(MCU) -f $(F_CPU:UL=) $<

upload: $(TARGET).hex
	avrdude -p $(MCU) $(AVRDUDE_PROG) -P $(PORT) -U flash:w:$<:i
