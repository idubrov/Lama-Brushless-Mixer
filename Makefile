#/usr/bin/make

AVRDUDE_FLAGS := -p t44 -c avrisp2 -P /dev/ttyUSB0 -v
CHIP := attiny44
CFLAGS := -Os -mmcu=$(CHIP)
LDFLAGS := -mmcu=$(CHIP)
CC := avr-gcc
OBJCOPY := avr-objcopy

.PHONY : all
all : mixer.hex

.PHONY : prog
prog : mixer.dude

.PHONY : clean
clean :
	@-rm -f *.hex *.o *.elf

%.elf : %.o
	$(CC) $(LDFLAGS) $< $(LOADLIBES) $(LDLIBS) -o $@

%.hex : %.elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

%.dude : %.hex
	avrdude $(AVRDUDE_FLAGS) -U flash:w:$<:a
