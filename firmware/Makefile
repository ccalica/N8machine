# Makefile N8 firmware

# C and assem files
# SRC =	main.c acia.s interrupt.s vectors.s
SRC = main.s init.s interrupt.s vectors.s

# compiler output
OBJ = N8firmware.bin

# build tools & options
CL65 = cl65
CLFLAGS  = -t none -O --cpu 6502 -C n8.cfg -m $(OBJ).map
LIB = n8.lib

# Targets
all: $(OBJ)

$(OBJ): $(SRC)
	$(CL65) $(CLFLAGS) -o $(OBJ) $(SRC) $(LIB)

clean:
	-rm -f *.o $(OBJ) $(OBJ).hex $(OBJ).map
