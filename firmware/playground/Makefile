# Makefile for playground monitor programs
#
# Each program builds as a standalone binary with symbol file.
# Usage:
#   make              Build all programs
#   make mon1         Build just mon1
#   make clean        Remove all build artifacts

CL65    = cl65
CLFLAGS = -vm -t none --cpu 6502 -C playground.cfg -Wl --dbgfile,$(@).dbg
CFG     = playground.cfg
COMMON  = common_init.s common_vectors.s

PROGS = mon1 mon2 mon3

.PHONY: all clean

all: $(PROGS)

$(PROGS): %: %.s $(COMMON) $(CFG)
	$(CL65) $(CLFLAGS) -m $@.map -Ln $@.sym -o $@ $< $(COMMON)

clean:
	-rm -f $(PROGS) *.o *.map *.sym *.dbg
