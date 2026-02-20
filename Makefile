#
# Cross Platform Makefile
# Compatible with MSYS2/MINGW, Ubuntu 14.04.1 and Mac OS X
#
# You will need SDL2 (http://www.libsdl.org):
# Linux:
#   apt-get install libsdl2-dev
# Mac OS X:
#   brew install sdl2
# MSYS2:
#   pacman -S mingw-w64-i686-SDL2
#

#CXX = g++
#CXX = clang++

EXE = n8
IMGUI_DIR = imgui
SRC_DIR = src
BUILD_DIR = build
SOURCES = $(SRC_DIR)/main.cpp $(SRC_DIR)/emulator.cpp $(SRC_DIR)/emu_tty.cpp $(SRC_DIR)/emu_dis6502.cpp
SOURCES +=$(SRC_DIR)/emu_labels.cpp $(SRC_DIR)/gui_console.cpp $(SRC_DIR)/utils.cpp $(SRC_DIR)/gdb_stub.cpp
SOURCES += $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_demo.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_widgets.cpp
SOURCES += $(IMGUI_DIR)/backends/imgui_impl_sdl2.cpp $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
_OBJS = $(addsuffix .o, $(basename $(notdir $(SOURCES))))
OBJS = $(patsubst %, $(BUILD_DIR)/%, $(_OBJS))
UNAME_S := $(shell uname -s)
LINUX_GL_LIBS = -lGL

CXXFLAGS = -std=c++11 -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends
CXXFLAGS += -g -Wall -Wformat -pthread -DENABLE_GDB_STUB=1
DEPFLAGS = -MMD -MP
LIBS =

##---------------------------------------------------------------------
## OPENGL ES
##---------------------------------------------------------------------

## This assumes a GL ES library available in the system, e.g. libGLESv2.so
# CXXFLAGS += -DIMGUI_IMPL_OPENGL_ES2
# LINUX_GL_LIBS = -lGLESv2
## If you're on a Raspberry Pi and want to use the legacy drivers,
## use the following instead:
# LINUX_GL_LIBS = -L/opt/vc/lib -lbrcmGLESv2

##---------------------------------------------------------------------
## BUILD FLAGS PER PLATFORM
##---------------------------------------------------------------------

ifeq ($(UNAME_S), Linux) #LINUX
	ECHO_MESSAGE = "Linux"
	LIBS += $(LINUX_GL_LIBS) -ldl `sdl2-config --libs`

	CXXFLAGS += `sdl2-config --cflags`
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(UNAME_S), Darwin) #APPLE
	ECHO_MESSAGE = "Mac OS X"
	LIBS += -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo `sdl2-config --libs`
	LIBS += -L/usr/local/lib -L/opt/local/lib

	CXXFLAGS += `sdl2-config --cflags`
	CXXFLAGS += -I/usr/local/include -I/opt/local/include
	CFLAGS = $(CXXFLAGS)
endif

ifeq ($(OS), Windows_NT)
    ECHO_MESSAGE = "MinGW"
    LIBS += -lgdi32 -lopengl32 -limm32 `pkg-config --static --libs sdl2`

    CXXFLAGS += `pkg-config --cflags sdl2`
    CFLAGS = $(CXXFLAGS)
endif

##---------------------------------------------------------------------
## BUILD RULES
##---------------------------------------------------------------------

$(BUILD_DIR)/%.o:%.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o:$(IMGUI_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o:$(IMGUI_DIR)/backends/%.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o:$(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

.PHONY: firmware

all: $(EXE)
	@echo Build complete for $(ECHO_MESSAGE)

$(EXE): $(OBJS)
	$(CXX) -o $@ $^ $(CXXFLAGS) $(LIBS)

firmware:
	make -C firmware install

-include $(OBJS:.o=.d)

clean:
	rm -f $(EXE) $(OBJS) $(OBJS:.o=.d)
	make -C firmware clean

##---------------------------------------------------------------------
## TEST BUILD
##---------------------------------------------------------------------

TEST_DIR = test
TEST_BUILD_DIR = build/test
TEST_EXE = n8_test

# Production source objects reused by test binary (gdb_stub.o compiled separately with test flags)
TEST_SRC_OBJS = $(BUILD_DIR)/emulator.o $(BUILD_DIR)/emu_tty.o \
                $(BUILD_DIR)/emu_dis6502.o $(BUILD_DIR)/emu_labels.o \
                $(BUILD_DIR)/utils.o $(TEST_BUILD_DIR)/gdb_stub.o

# Test source files
TEST_SOURCES = $(wildcard $(TEST_DIR)/*.cpp)
_TEST_OBJS = $(addsuffix .o, $(basename $(notdir $(TEST_SOURCES))))
TEST_OBJS = $(patsubst %, $(TEST_BUILD_DIR)/%, $(_TEST_OBJS))

# Test compiler flags: same C++ standard, add test/ and src/ to include path
TEST_CXXFLAGS = -std=c++11 -g -Wall -Wformat -I$(SRC_DIR) -I$(TEST_DIR) -DGDB_STUB_TESTING

$(TEST_BUILD_DIR):
	mkdir -p $(TEST_BUILD_DIR)

# gdb_stub compiled with test flags (GDB_STUB_TESTING is in TEST_CXXFLAGS)
$(TEST_BUILD_DIR)/gdb_stub.o: $(SRC_DIR)/gdb_stub.cpp | $(TEST_BUILD_DIR)
	$(CXX) $(TEST_CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

$(TEST_BUILD_DIR)/%.o: $(TEST_DIR)/%.cpp | $(TEST_BUILD_DIR)
	$(CXX) $(TEST_CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

.PHONY: test clean-test

test: $(TEST_EXE)
	./$(TEST_EXE) < /dev/null

$(TEST_EXE): $(TEST_SRC_OBJS) $(TEST_OBJS)
	$(CXX) -o $@ $^

-include $(TEST_BUILD_DIR)/gdb_stub.d
-include $(TEST_OBJS:.o=.d)

clean-test:
	rm -f $(TEST_EXE) $(TEST_BUILD_DIR)/*.o $(TEST_BUILD_DIR)/*.d
