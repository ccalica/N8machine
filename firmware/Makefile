# Makefile N8 firmware — kernel + monitor

CL65     = cl65
LIB      = n8.lib
DEST_DIR = ..

# --- Kernel ($F000-$FFFF) ---
KERNEL_SRC = init.s tty.s console.s interrupt.s vectors.s kentry.s
KERNEL_OBJ = n8_kernel
KERNEL_CFG = kernel.cfg
KERNEL_FLAGS = -vm -t none -O --cpu 6502 -C $(KERNEL_CFG) -m $(KERNEL_OBJ).map -Ln $(KERNEL_OBJ).sym

# --- Monitor ($E000-$EFFF) ---
MONITOR_SRC = monitor.s
MONITOR_OBJ = n8_monitor
MONITOR_CFG = monitor.cfg
MONITOR_FLAGS = -vm -t none --cpu 6502 -C $(MONITOR_CFG) -m $(MONITOR_OBJ).map -Ln $(MONITOR_OBJ).sym

# Targets
all: $(KERNEL_OBJ) $(MONITOR_OBJ)

install: all
	cp $(KERNEL_OBJ) $(DEST_DIR)
	cp $(KERNEL_OBJ).sym $(DEST_DIR)
	cp $(MONITOR_OBJ) $(DEST_DIR)
	cp $(MONITOR_OBJ).sym $(DEST_DIR)

$(KERNEL_OBJ): $(KERNEL_SRC)
	$(CL65) $(KERNEL_FLAGS) -o $(KERNEL_OBJ) $(KERNEL_SRC) $(LIB)

$(MONITOR_OBJ): $(MONITOR_SRC)
	$(CL65) $(MONITOR_FLAGS) -o $(MONITOR_OBJ) $(MONITOR_SRC)

clean:
	-rm -f *.o $(KERNEL_OBJ) $(KERNEL_OBJ).sym $(KERNEL_OBJ).map
	-rm -f $(MONITOR_OBJ) $(MONITOR_OBJ).sym $(MONITOR_OBJ).map
