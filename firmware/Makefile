# Makefile N8 firmware — kernel + shell

CL65     = cl65
LIB      = n8.lib
DEST_DIR = ..

# --- Kernel ($F000-$FFFF) ---
KERNEL_SRC = init.s tty.s console.s interrupt.s vectors.s kentry.s
KERNEL_OBJ = n8_kernel
KERNEL_CFG = kernel.cfg
KERNEL_FLAGS = -vm -t none -O --cpu 6502 -C $(KERNEL_CFG) -m $(KERNEL_OBJ).map -Ln $(KERNEL_OBJ).sym

# --- Shell ($E000-$EFFF) ---
SHELL_SRC = shell.s
SHELL_OBJ = n8_shell
SHELL_CFG = shell.cfg
SHELL_FLAGS = -vm -t none --cpu 6502 -C $(SHELL_CFG) -m $(SHELL_OBJ).map -Ln $(SHELL_OBJ).sym

# Targets
all: $(KERNEL_OBJ) $(SHELL_OBJ)

install: all
	cp $(KERNEL_OBJ) $(DEST_DIR)
	cp $(KERNEL_OBJ).sym $(DEST_DIR)
	cp $(SHELL_OBJ) $(DEST_DIR)
	cp $(SHELL_OBJ).sym $(DEST_DIR)

$(KERNEL_OBJ): $(KERNEL_SRC)
	$(CL65) $(KERNEL_FLAGS) -o $(KERNEL_OBJ) $(KERNEL_SRC) $(LIB)

$(SHELL_OBJ): $(SHELL_SRC)
	$(CL65) $(SHELL_FLAGS) -o $(SHELL_OBJ) $(SHELL_SRC)

clean:
	-rm -f *.o $(KERNEL_OBJ) $(KERNEL_OBJ).sym $(KERNEL_OBJ).map
	-rm -f $(SHELL_OBJ) $(SHELL_OBJ).sym $(SHELL_OBJ).map
