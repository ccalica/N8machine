# Makefile for Sprawl Adventure
#
# Multi-file build: adventure.s (engine) + world.s (data)
# Uses shared playground config and startup files from parent dir.
#
# Usage:
#   make              Build adventure binary + symbol file
#   make clean        Remove build artifacts

CL65    = cl65
CA65    = ca65
LD65    = ld65
CAFLAGS = -t none --cpu 6502
CFG     = ../playground.cfg
COMMON  = ../common_init.s ../common_vectors.s

TARGET  = adventure
SOURCES = adventure.s world.s
COMMON_SRCS = ../common_init.s ../common_vectors.s
ALL_SRCS = $(SOURCES) $(COMMON_SRCS)
OBJECTS = $(SOURCES:.s=.o) common_init.o common_vectors.o

.PHONY: all clean

all: $(TARGET)

# Assemble each source file
adventure.o: adventure.s
	$(CA65) $(CAFLAGS) -o $@ $<

world.o: world.s
	$(CA65) $(CAFLAGS) -o $@ $<

common_init.o: ../common_init.s
	$(CA65) $(CAFLAGS) -o $@ $<

common_vectors.o: ../common_vectors.s
	$(CA65) $(CAFLAGS) -o $@ $<

# Link
$(TARGET): $(OBJECTS) $(CFG)
	$(LD65) -C $(CFG) -m $(TARGET).map -Ln $(TARGET).sym --dbgfile $(TARGET).dbg -o $(TARGET) $(OBJECTS)

clean:
	-rm -f $(TARGET) *.o *.map *.sym *.dbg
