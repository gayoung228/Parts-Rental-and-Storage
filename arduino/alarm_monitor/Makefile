MCU = atmega328p
F_CPU = 16000000UL
BUZZER_ACTIVE ?= 0
CC = avr-gcc
CFLAGS = -std=c11 -Os -Wall -Wextra -Werror -mmcu=$(MCU) -DF_CPU=$(F_CPU) -DBUZZER_ACTIVE=$(BUZZER_ACTIVE) -I../../tools

all: alarm_monitor.hex

alarm_monitor.elf: main.c alarm_state.c alarm_state.h ../../tools/rental_protocol.h FORCE
	$(CC) $(CFLAGS) main.c alarm_state.c -o $@
	avr-size $@

alarm_monitor.hex: alarm_monitor.elf
	avr-objcopy -O ihex -R .eeprom $< $@

FORCE:

.PHONY: all FORCE
