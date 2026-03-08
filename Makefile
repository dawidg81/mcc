.PHONY: all clean

all: mcc

mcc: build/*.o
	g++ build/*.o -o mcc -lpthread -lws2_32

build/%.o: src/%.cpp
	mkdir -p build
	g++ -c src/*.cpp -o build/

clean:
	rm build
	rm mcc.exe
