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
SRC_DIR  := usr-code
BIN_DIR  := rootfs/bin
OBJ_DIR  := usr-code/obj
BUILD_DIR := build
APP_SRC_DIR := usr-code/app
include config.cfg

# 目标文件
TARGET   := $(BIN_DIR)/mysh
OBJS     := $(OBJ_DIR)/mysh.o
DEPS     := $(OBJS:.o=.dep)

-include $(wildcard $(DEPS))

# 动态源文件路径
SOURCE := $(SRC_DIR)/$(MYSHELL_SRC_DIR)/mysh.cpp

# 编译规则
$(OBJ_DIR)/%.o: $(SOURCE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) $< -o $@

MAKEFLAGS += --no-print-directory

.PHONY: default build user initramfs 
.PHONY: run run-nographic run-bridge debug
.PHONY: clean info help

default: build
build:
	@mkdir -p $(BIN_DIR)
	@$(MAKE) -j user initramfs
	@echo "\033[32m===== 构建完成 =====\n\033[0m"
build-order:
	@mkdir -p $(BIN_DIR)
	@$(MAKE) user initramfs

# 安装应用程序 APP
user: $(addprefix build-,$(PROJECTS))
$(addprefix build-,$(PROJECTS)): build-%:
	@echo "===== 构建项目：$* ====="
	@$(MAKE) -C $(APP_SRC_DIR)/$* install APP_BUILD_BIN=../../../$(APP_BUILD_BIN)

# 生成 initramfs
initramfs: $(TARGET)
	@echo "===== 生成 initramfs ====="
	@mkdir -p $(BUILD_DIR)
	cd initramfs && find . -print0 | cpio --null -ov --format=newc | gzip -9 \
	  > ../$(BUILD_DIR)/initramfs.cpio.gz
	@echo "===== initramfs 已成功生成至 $(BUILD_DIR)/initramfs.cpio.gz ====="

run:
	sh -c "bash ./tools/run-qemu.sh --display window --bios uefi --ext4"

run-nographic:
	sh -c "bash ./tools/run-qemu.sh --display nographic --bios legacy --ext4"

run-user:
	sh -c "bash ./tools/run-qemu.sh --display nographic --bios legacy --user --ext4"

run-bridge:
	@if ip link show type bridge br0 >/dev/null 2>&1; then \
		echo "[SUCCESS] 网桥 br0 已存在"; \
	else \
		echo "[INFO] 网桥 br0 不存在, 正在创建..."; \
		$(MAKE) bridge; \
		if ! ip link show type bridge br0 >/dev/null 2>&1; then \
            echo "[FATAL] 网桥 br0 创建失败！"; \
            exit 1; \
        fi; \
		echo "[SUCCESS] 网桥 br0 创建成功"; \
		sleep 3; \
	fi
	sudo sh -c "bash ./tools/run-qemu.sh --display nographic --bios legacy --bridge --ext4"

# # sudo ln ~/linux/vmlinuz_debug ~/path/to/Linux-minimal/vmlinuz_my4debug-ln
# gdb vmlinuz_my4debug-ln -ex 'target remote localhost:1234'
# (gdb) info r # CPU RESET
# (gdb) b *0x1000000
debug:
	sh -c "bash ./tools/run-qemu.sh --display nographic --ext4 --debug"

bridge:
	sudo ./tools/bridge-setup.sh
bridge-clean:
	sudo ./tools/bridge-clean.sh

clean:
	@echo "===== 清理构建目录 ====="
	@rm -rf $(OBJ_DIR) $(TARGET) $(BUILD_DIR)
	@for app in $(PROJECTS); do \
		echo "清理 $$app"; \
		if [ -f $(BIN_DIR)/$$app ]; then \
			$(MAKE) -C $(APP_SRC_DIR)/$$app clean APP_BUILD_BIN=../../../$(APP_BUILD_BIN); \
		fi \
	done
	@echo "===== 清理网桥 ====="
	${MAKE} bridge-clean
	@echo "===== 清理完成 ====="

info: 
	@echo "(当前shell: $(MYSHELL_SRC_DIR))"

help:
	@echo "可用目标:"
	@echo "  make build                等同于 make user && make initramfs (当前shell: $(MYSHELL_SRC_DIR))"
	@echo "  make MYSHELL=myshell      使用C-nostdlib myshell编译"
	@echo "  make MYSHELL=myshell-cpp  使用C++        myshell编译"
	@echo "  make user                 不编译initramfs，构建应用程序"
	@echo "  make initramfs            构建初始化内存盘"
	@echo "  make run                  启动 QEMU (有图形界面)"
	@echo "  make run-nographic        启动 QEMU (无图形界面)"
	@echo "  make run-bridge           检测并创建 br0 网桥后启动 QEMU"
	@echo "  make debug                启动 QEMU 内核调试模式"
	@echo "  make clean                清理构建文件"	