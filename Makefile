.PHONY: all clean

SRC = $(wildcard src/*.cpp)
OBJ = $(patsubst src/%.cpp,build/%.o,$(SRC))

# Detect Windows
ifeq ($(OS),Windows_NT)
    MKDIR = if not exist build mkdir build
    RM = rmdir /s /q build & del /q mcc.exe
else
    MKDIR = mkdir -p build
    RM = rm -rf build mcc
endif

all: mcc

mcc: $(OBJ)
	g++ $(OBJ) -o mcc -lpthread -lws2_32

build/%.o: src/%.cpp
	$(MKDIR)
	g++ -c $< -o $@

clean:
	$(RM)
