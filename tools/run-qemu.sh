#!/bin/bash
ARCH="x86_64"
QEMU=$(which qemu-system-${ARCH})

QEMU_DEVICES=""
QEMU_DISK_IMAGE="build/rootfs.img"
QEMU_SMP="2,cores=2,threads=1,sockets=1"
QEMU_MEMORY="128M"
QEMU_ARGUMENT=""
QEMU_ARGUMENT+=" -smp ${QEMU_SMP} -m ${QEMU_MEMORY} "
QEMU_ARGUMENT+=" -kernel vmlinuz -initrd build/initramfs.cpio.gz "
APPEND_PARAMS=" console=ttyS0 quiet acpi=off "

# 设置无图形界面模式
QEMU_NOGRAPHIC=false
TAP_DEV="tap0"

while true;do
    case "$1" in
        --display)
        case "$2" in
            window)
            QEMU_ARGUMENT+=" -display gtk "
            ;;
            nographic)
            QEMU_NOGRAPHIC=true
            QEMU_ARGUMENT+=" -nographic -serial mon:stdio "
            ;;
            vnc)
            QEMU_ARGUMENT+=" -display vnc=:00 "
            ;;
        esac;shift 2;;
        --debug)
        QEMU_ARGUMENT+=" -S -s "
        APPEND_PARAMS+=" nokaslr "
        shift 1;;
        --bridge)
        QEMU_DEVICES+=" -device e1000,netdev=net0,mac=20:50:00:12:34:56 "
        QEMU_DEVICES+=" -netdev tap,id=net0,ifname=${TAP_DEV},vhost=on,script=no,downscript=no "
        # QEMU_DEVICES+=" -net nic -net tap,ifname=${TAP_DEV},vhost=on,script=no,downscript=no "
        APPEND_PARAMS+=" NET_MODE=bridge "
        shift 1;;
        --user)
        QEMU_DEVICES+=" -device e1000,netdev=hostnet1,id=net1 "
        QEMU_DEVICES+=" -netdev user,id=hostnet1,hostfwd=tcp::12580-:12580,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80,hostfwd=tcp::2233-:2233 "
        APPEND_PARAMS+=" NET_MODE=user "
        shift 1;;
        --usb)
        QEMU_DEVICES+=" -usb -device nec-usb-xhci,id=xhci -drive file=disk.qcow2,if=none,id=usbdisk -device usb-storage,drive=usbdisk,bus=xhci.0 "
        shift 1;;
        --ext4)
        QEMU_DEVICES+=" -drive file=${QEMU_DISK_IMAGE},format=raw,id=disk0,if=none "
        QEMU_DEVICES+=" -device ide-hd,drive=disk0 "
        # QEMU_DEVICES+=" -device virtio-blk-pci,drive=disk0 "
        APPEND_PARAMS+=" root=/dev/sda init=/init "
        shift 1;;
        *) break
      esac
  done

QEMU_ARGUMENT+=" -append '${APPEND_PARAMS}' "
QEMU_ARGUMENT+=" -D ../qemu.log "

TMP_LOOP_DEVICE=""

mount_disk_image() {
    echo "正在挂载磁盘镜像..."
    TMP_LOOP_DEVICE=$(sudo losetup -f --show -P ${QEMU_DISK_IMAGE}) || exit 1
    # 根据函数入参判断是否需要格式化磁盘镜像
    if [ "$1" == "mnt" ]; then
        mkdir -p mnt
        sudo mount ${TMP_LOOP_DEVICE} mnt
    fi
    echo "挂载磁盘镜像完成"
}
umount_disk_image() {
    echo "正在卸载磁盘镜像..."
    if [ "$1" == "mnt" ]; then
        sudo umount mnt
    fi
    sudo losetup -d ${TMP_LOOP_DEVICE} || (echo "卸载磁盘镜像失败" && exit 1)
    echo "卸载磁盘镜像完成"
}

prepare_disk_image() {
    # 如果磁盘镜像不存在，则创建磁盘镜像
    echo "正在准备磁盘镜像..."
    if [ ! -f ${QEMU_DISK_IMAGE} ]; then
        echo "正在创建磁盘镜像..."
        qemu-img create -f raw ${QEMU_DISK_IMAGE} 128M || (echo "创建磁盘镜像失败" && exit 1)
        mkfs.ext4 ${QEMU_DISK_IMAGE}

        mount_disk_image mnt
        echo "loop device: ${TMP_LOOP_DEVICE}"

        echo "正在格式化磁盘镜像..."
        sudo mkdir -p mnt/{bin,sbin,usr,usr/bin,usr/sbin,etc,etc/init.d,lib,proc,sys,dev,run,var}
        sudo cp initramfs/bin/busybox mnt/bin
        sudo chroot mnt /bin/busybox --install -s
        sudo chmod 755 mnt/bin/busybox

        umount_disk_image mnt

        echo "Successfully mkfs"
        chmod 777 ${QEMU_DISK_IMAGE}
        echo "创建磁盘镜像完成"
    fi
    echo "磁盘镜像已经准备好"
}
write_disk_image() {
    echo "正在写入磁盘镜像..."
    mount_disk_image mnt

    sudo cp -r rootfs/* mnt/

    umount_disk_image mnt
    echo "写入磁盘镜像完成"
}

run_qemu() {
    sh -c "${QEMU} ${QEMU_ARGUMENT} ${QEMU_DEVICES}"
}

main() {
    prepare_disk_image
    write_disk_image
    # sleep 3
    run_qemu
}

main