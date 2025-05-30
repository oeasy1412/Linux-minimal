# 用户指定的shell位置，默认使用 `myshell-cpp`
MYSHELL ?= myshell-cpp
# MYSHELL ?= myshell # without libc

# 验证目录有效性
VALID_MYSHELLS := myshell myshell-cpp
ifneq ($(filter $(MYSHELL),$(VALID_MYSHELLS)),)
MYSHELL_SRC_DIR := $(MYSHELL)
else
$(error 无效的MYSHELL: $(MYSHELL). 有效选项: myshell 或 myshell-cpp)
endif

# 根据shell类型配置工具链
ifeq ($(MYSHELL),myshell)
  CC       := gcc
  LD       := ld
  CXXFLAGS := -g -O2 -ffreestanding -nostdlib -fno-exceptions -I.
  LDFLAGS  := 
else
  CC       := g++
  LD       := g++
  CXXFLAGS := -g -O2 -I.
  LDFLAGS  := -static
endif
DEPFLAGS = -MT $@ -MMD -MP -MF $(OBJ_DIR)/$*.d

# 路径配置
SRC_DIR  := initramfs/code
BIN_DIR  := initramfs/bin
OBJ_DIR  := initramfs/code/obj
BUILD_DIR := build

# 目标文件
TARGET   := $(BIN_DIR)/mysh
OBJS     := $(OBJ_DIR)/mysh.o
DEPS     := $(OBJS:.o=.dep)

-include $(wildcard $(DEPS))

.PHONY: default initramfs run run-nographic clean info help
default: initramfs

# 动态源文件路径
SOURCE := $(SRC_DIR)/$(MYSHELL_SRC_DIR)/mysh.cpp

# 编译规则
$(OBJ_DIR)/%.o: $(SOURCE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) $< -o $@

# 生成 initramfs
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

info: 
	@echo "(当前shell: $(MYSHELL_SRC_DIR))"

help:
	@echo "可用目标:"
	@echo "  make                      等同于 make initramfs (当前shell: $(MYSHELL_SRC_DIR))"
	@echo "  make MYSHELL=myshell      使用C-nostdlib myshell编译"
	@echo "  make MYSHELL=myshell-cpp  使用C++        myshell编译"
	@echo "  make initramfs            构建初始化内存盘"
	@echo "  make run                  启动 QEMU (有图形界面)"
	@echo "  make run-nographic        启动 QEMU (无图形界面)"
	@echo "  make clean                清理构建文件"