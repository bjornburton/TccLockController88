# Project Name
TARGET:= tcclc

# Which target board
# BOARD:=mh-timy
BOARD:=t88

# Which microcontroller
MCU:=attiny88

# Which avrdude to use
PROGSW:=avrdude

# Which fuses
#BOD ≈ 4.3 V: EFUSE = 0xFC (BODLEVEL = 100)
LFUSE:= lfuse:w:0xFF:m 
HFUSE:= hfuse:w:0xDF:m 
EFUSE:= efuse:w:0xF8:m

# Apps and Flags
PROGSWFLAGS:= -p $(BOARD)
CC       := avr-gcc 
CFLAGS   :=  -std=c99 -g -mmcu=$(MCU) -Wall -Os -pedantic 
CONV     :=avr-objcopy
CONVFLAGS:= -j .text -j .data -O ihex
LIBS     :=

# Build filenames
HEADERS := $(TARGET).h
OBJECTS := $(TARGET).o
HEX     := $(TARGET).hex
ELF     := $(TARGET).elf
CSOURCES:= $(TARGET).c

# The usual make stuff
default: $(HEX)
elf: $(ELF)
all: default

$(OBJECTS): $(CSOURCES)
	$(CC) -c $(CFLAGS) $(CSOURCES)

$(ELF): $(OBJECTS)
	$(CC) $(LIBS) $(OBJECTS) $(CFLAGS) -o $(ELF)
	chmod -x $(ELF)

$(HEX): $(ELF)
	$(CONV) $(CONVFLAGS) $(ELF) $(HEX) 

clean:
	-rm -f $(OBJECTS)
	-rm -f $(ELF)
	
install:
	$(PROGSW) $(PROGSWFLAGS) -c usbtiny -U flash:w:$(HEX)

installice:
	$(PROGSW) $(PROGSWFLAGS) -c atmelice_isp -P usb -U flash:w:$(HEX)

installasp:
	$(PROGSW) -p $(MCU) -c usbasp -U flash:w:$(HEX)

size:
	avr-size --format=avr --mcu=$(MCU) $(ELF)

fuse:
	$(PROGSW) $(PROGSWFLAGS) -c atmelice_isp -P usb -u -U $(LFUSE)  -U $(HFUSE) -U $(EFUSE)

