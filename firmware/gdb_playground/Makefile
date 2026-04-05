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

TESTS = test_regs test_breakpoints test_memory test_stack test_counter test_zeropage test_tty test_monitor test_benchmark test_viddata test_keyecho test_console test_storage demo_console demo_tui

.PHONY: all clean

all: $(TESTS)

$(TESTS): %: %.s $(COMMON) $(CFG)
	$(CL65) $(CLFLAGS) -m $@.map -Ln $@.sym -o $@ $< $(COMMON)

# Demo: separate build (no common_init/vectors — has its own)
test_demo: test_demo.s demo.cfg
	$(CL65) -vm -t none --cpu 6502 -C demo.cfg -Wl --dbgfile,$(@).dbg -m $@.map -Ln $@.sym -o $@ $<

clean:
	-rm -f $(TESTS) test_demo *.o *.map *.sym *.dbg *.vec
