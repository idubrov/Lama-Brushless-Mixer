#/usr/bin/make
# avrdude -p t44 -c avrisp2 -P /dev/ttyUSB0 -v -U flash:w:mixer.hex:a

ASM := avra
AVRDUDE_FLAGS := -p t44 -c avrisp2 -P /dev/ttyUSB0 -v

.PHONY : all prog clean

all : mixer.hex

prog : mixer.dude

clean :
	@-rm -f *.hex *.cof *.obj

%.hex : %.asm
	$(ASM) $< -o $@

%.dude : %.hex
	avrdude $(AVRDUDE_FLAGS) -U flash:w:$<:a
