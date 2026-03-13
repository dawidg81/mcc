.PHONY: all clean

SRC = $(wildcard src/*.cpp)
OBJ = $(patsubst src/%.cpp,build/%.o,$(SRC))

# Detect Windows
ifeq ($(OS),Windows_NT)
    MKDIR = if not exist build mkdir build
    RM = rmdir /s /q build & del /q ccraft2.exe
else
    MKDIR = mkdir -p build
    RM = rm -rf build ccraft2
endif

all: ccraft2

ccraft2: $(OBJ)
	g++ -std=c++14 $(OBJ) -o ccraft2 -lpthread -lws2_32 -lz

build/%.o: src/%.cpp
	$(MKDIR)
	g++ -std=c++14 -c $< -o $@

clean:
	$(RM)
