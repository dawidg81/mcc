CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra
LDFLAGS  :=

ifeq ($(OS), Windows_NT)
    LDFLAGS += -lws2_32
    TARGET  := mcc.exe
else
    TARGET  := mcc
endif

BUILD_DIR := build
SRC_DIR   := src

SRCS := $(SRC_DIR)/main.cpp \
        $(SRC_DIR)/Core/Logger.cpp \
        $(SRC_DIR)/Network/Socket.cpp

OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))

INCLUDES := -I$(SRC_DIR)/Core \
            -I$(SRC_DIR)/Network

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
