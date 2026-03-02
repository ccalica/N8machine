# Makefile for Sprawl Adventure
#
# Multi-file build: adventure.s (engine) + world.s (data)
# Uses adventure.cfg to produce two output files:
#   adventure       ROM engine (4KB at $E000, kernel at $F000)
#   adventure_data  RAM game data (~47KB at $0500)
#
# Usage:
#   make              Build adventure + adventure_data
#   make clean        Remove build artifacts

CL65    = cl65
CA65    = ca65
LD65    = ld65
CAFLAGS = -t none --cpu 6502
CFG     = adventure.cfg

TARGET  = adventure
SOURCES = adventure.s world.s
COMMON_SRCS = ../common_init.s
ALL_SRCS = $(SOURCES) $(COMMON_SRCS)
# common_init.o first so _init lands at $E000 (kernel JMPs to $E000)
OBJECTS = common_init.o $(SOURCES:.s=.o)

.PHONY: all clean

all: $(TARGET)

# Assemble each source file
adventure.o: adventure.s
	$(CA65) $(CAFLAGS) -o $@ $<

world.o: world.s
	$(CA65) $(CAFLAGS) -o $@ $<

common_init.o: ../common_init.s
	$(CA65) $(CAFLAGS) -o $@ $<

# Link
$(TARGET): $(OBJECTS) $(CFG)
	$(LD65) -C $(CFG) -m $(TARGET).map -Ln $(TARGET).sym --dbgfile $(TARGET).dbg -o $(TARGET) $(OBJECTS)

clean:
	-rm -f $(TARGET) adventure_data *.o *.map *.sym *.dbg
