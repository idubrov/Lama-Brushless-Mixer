#/usr/bin/make

AVRDUDE_FLAGS := -p t44 -c avrisp2 -P /dev/ttyUSB0 -v
CHIP := attiny44
CFLAGS := -Os -Wall -std=c99 -mmcu=$(CHIP)
LDFLAGS := -mmcu=$(CHIP)
CC := avr-gcc
OBJCOPY := avr-objcopy

.PHONY : all
all : mixer.hex

.PHONY : install
install : mixer.install

.PHONY : install.eeprom
install.eeprom : mixer.install.eeprom

.PHONY : install.fuse
install.fuse : mixer.install.fuse

.PHONY : clean
clean :
	@-rm -f *.hex *.o *.elf *.fuse *.lfuse *.hfuse *.efuse *.eeprom

mixer.o : config.h

%.elf : %.o
	$(CC) $(LDFLAGS) $< $(LOADLIBES) $(LDLIBS) -o $@

%.hex : %.elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

%.fuse : %.elf
	$(OBJCOPY) -j .fuse -O binary $< $@

%.eeprom : %.elf
	$(OBJCOPY) -j .eeprom -O ihex $< $@

%.lfuse : %.fuse
	dd if=$< of=$@ skip=0 bs=1 count=1

%.hfuse : %.fuse
	dd if=$< of=$@ skip=1 bs=1 count=1

%.efuse : %.fuse
	dd if=$< of=$@ skip=2 bs=1 count=1

%.install_fuse : %.lfuse %.hfuse %.efuse
	avrdude $(AVRDUDE_FLAGS) -U lfuse:w:$*.lfuse:r
	avrdude $(AVRDUDE_FLAGS) -U hfuse:w:$*.hfuse:r
	avrdude $(AVRDUDE_FLAGS) -U efuse:w:$*.efuse:r

%.install : %.hex
	avrdude $(AVRDUDE_FLAGS) -U flash:w:$<:a

%.install.eeprom : %.eeprom
	avrdude $(AVRDUDE_FLAGS) -U eeprom:w:$<:a
