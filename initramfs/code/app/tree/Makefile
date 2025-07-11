SRC = $(wildcard *.c) $(wildcard *.cpp)
TARGET = tree

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	@g++ -O2 -static -std=c++17 -o $@ $^
	@echo "$(TARGET) 构建完成"

clean:
	rm -f $(TARGET) $(APP_BUILD_BIN)/$(TARGET)

install: $(TARGET)
	mv $(TARGET) $(APP_BUILD_BIN)