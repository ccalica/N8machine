# Makefile for gdb_playground test programs
#
# Each test builds as a standalone binary with symbol file.
# Usage:
#   make              Build all test programs
#   make test_regs    Build just test_regs
#   make clean        Remove all build artifacts

CL65    = cl65
CLFLAGS = -vm -t none --cpu 6502 -C playground.cfg -Wl --dbgfile,$(@).dbg
CFG     = playground.cfg
COMMON  = common_init.s common_vectors.s

TESTS = test_regs test_breakpoints test_memory test_stack test_counter test_zeropage test_tty

.PHONY: all clean

all: $(TESTS)

$(TESTS): %: %.s $(COMMON) $(CFG)
	$(CL65) $(CLFLAGS) -m $@.map -Ln $@.sym -o $@ $< $(COMMON)

clean:
	-rm -f $(TESTS) *.o *.map *.sym *.dbg
