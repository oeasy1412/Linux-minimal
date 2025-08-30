SRC = $(wildcard *.c) $(wildcard *.cpp)
TARGET = $(APP_BUILD_BIN)/tree

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	@g++ -O2 -static -std=c++17 -o $@ $^
	@echo "\033[32m$(notdir $(TARGET)) 构建完成\033[0m"

clean:
	rm -f $(TARGET)

install: all