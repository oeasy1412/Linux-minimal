# 工具链配置
CXX      := gcc
LD       := ld
CXXFLAGS := -g -O2 -ffreestanding -nostdlib -fno-exceptions -I.
DEPFLAGS = -MT $@ -MMD -MP -MF $(OBJ_DIR)/$*.d

# 路径配置
SRC_DIR  := initramfs/code
BIN_DIR  := initramfs/bin
OBJ_DIR  := initramfs/code/obj
BUILD_DIR := build

# 目标文件
TARGET   := $(BIN_DIR)/mysh
OBJS     := $(OBJ_DIR)/mysh-xv6.o
DEPS     := $(OBJS:.o=.d)

-include $(DEPS)

.PHONY: default
default: initramfs

# 编译链接
$(OBJ_DIR)/%.o: $(SRC_DIR)/myshell/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@
$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(LD) $< -o $@

.PHONY: initramfs run run-nographic clean

# 生成 initramfs # $(shell mkdir -p build)
initramfs: $(TARGET)
	@mkdir -p $(BUILD_DIR)
	cd initramfs && find . -print0 | cpio --null -ov --format=newc | gzip -9 \
	  > ../$(BUILD_DIR)/initramfs.cpio.gz

run:
	@qemu-system-x86_64 \
	  -display gtk \
	  -m 128 \
	  -kernel vmlinuz \
	  -initrd build/initramfs.cpio.gz \
	  -append "console=ttyS0 quiet acpi=off"

run-nographic:
	@qemu-system-x86_64 \
	  -nographic \
	  -serial mon:stdio \
	  -m 128 \
	  -kernel vmlinuz \
	  -initrd build/initramfs.cpio.gz \
	  -append "console=ttyS0 quiet acpi=off"

clean:
	@rm -rf $(OBJ_DIR) $(TARGET) $(BUILD_DIR)

.PHONY: help
help:
	@echo "可用目标:"
	@echo "  make               等同于 make initramfs"
	@echo "  make initramfs     构建初始化内存盘"
	@echo "  make run           启动 QEMU (图形界面)"
	@echo "  make run-nographic 启动 QEMU (无图形界面)"
	@echo "  make clean         清理构建文件"