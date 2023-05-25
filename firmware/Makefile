# Makefile N8 firmware

# C and assem files
SRC = main.s init.s tty.s interrupt.s vectors.s

# compiler output
OBJ = N8firmware

# build tools & options
CL65 = cl65
CLFLAGS  = -vm -t none -O --cpu 6502 -C n8.cfg -m $(OBJ).map -Ln $(OBJ).sym
LIB = n8.lib
DEST_DIR = ..

# Targets
all: $(OBJ)

install: all
	cp $(OBJ) $(DEST_DIR)
	cp $(OBJ).sym $(DEST_DIR)

$(OBJ): $(SRC)
	$(CL65) $(CLFLAGS) -o $(OBJ) $(SRC) $(LIB)

clean:
	-rm -f *.o $(OBJ) $(OBJ).sym $(OBJ).map
