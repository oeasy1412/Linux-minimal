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
        APPEND_PARAMS+=" root=/dev/sda2 init=/init "
        shift 1;;
        *) break
      esac
  done

QEMU_ARGUMENT+=" -append '${APPEND_PARAMS}' "
QEMU_ARGUMENT+=" -D ../qemu.log "

TMP_LOOP_DEVICE=""
EFI_MNT="mnt/p1"
ROOT_MNT="mnt/p2"

mount_disk_image() {
    local MOUNT=$1
    echo "正在挂载磁盘镜像..."
    TMP_LOOP_DEVICE=$(sudo losetup -f --show -P ${QEMU_DISK_IMAGE}) || exit 1
    sudo partprobe ${TMP_LOOP_DEVICE}
    sleep 1
    echo "loop设备: ${TMP_LOOP_DEVICE}"
    # 根据函数入参判断是否需要格式化磁盘镜像
    if [ "${MOUNT}" == "mnt" ]; then
        mkdir -p ${MOUNT}
        # 遍历所有分区
        for PART in "${TMP_LOOP_DEVICE}"p*; do
            if [ -e "$PART" ]; then
                PART_NUMBER="${PART##*p}"
                MOUNT_DIR="mnt/p${PART_NUMBER}"
                sudo mkdir -p "${MOUNT_DIR}"
                sudo mount "$PART" "${MOUNT_DIR}"
            else
                echo "错误：分区设备 $PART 不存在 $BASE_DEV"
                sudo losetup -d "${TMP_LOOP_DEVICE}"
                exit 1
            fi
        done
    fi
    echo "挂载磁盘镜像完成"
}
umount_disk_image() {
    local MOUNT=$1
    echo "正在卸载磁盘镜像..."
    if [ "${MOUNT}" == "mnt" ]; then
        mkdir -p ${MOUNT}
        # 遍历所有分区
        for PART in "${TMP_LOOP_DEVICE}"p*; do
            if [ -e "$PART" ]; then
                PART_NUMBER="${PART##*p}"
                MOUNT_DIR="mnt/p${PART_NUMBER}"
                sudo umount "$PART" "${MOUNT_DIR}"
            else
                echo "错误：分区设备 $PART 不存在"
                sudo losetup -d "${TMP_LOOP_DEVICE}"
                exit 1
            fi
        done
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

        mount_disk_image
        echo "创建分区表..."
        sudo parted -s ${TMP_LOOP_DEVICE} mklabel gpt
        sudo parted -s ${TMP_LOOP_DEVICE} mkpart EFI fat32 1MiB 16MiB
        sudo parted -s ${TMP_LOOP_DEVICE} mkpart ROOT ext4 16MiB 100%
        sudo parted -s ${TMP_LOOP_DEVICE} set 1 esp on  # 设置EFI系统分区标志

        sudo partprobe ${TMP_LOOP_DEVICE}
        sudo blockdev --rereadpt ${TMP_LOOP_DEVICE}
        # sudo partx -v --update ${TMP_LOOP_DEVICE} || sudo partx -v --add ${TMP_LOOP_DEVICE}
        sleep 1

        echo "正在格式化磁盘镜像..."
        sudo mkfs.fat -F32 -n EFI ${TMP_LOOP_DEVICE}p1 || (echo "FAT32格式化失败")
        sudo mkfs.ext4 -F ${TMP_LOOP_DEVICE}p2 || (echo "EXT4格式化失败")
        mkdir -p mnt ${EFI_MNT} ${ROOT_MNT}
        sudo mount ${TMP_LOOP_DEVICE}p1 ${EFI_MNT}
        sudo mkdir -p ${EFI_MNT}/EFI ${EFI_MNT}/EFI/BOOT
        sudo echo "fs0:\EFI\BOOT\bootx64.efi" > ${EFI_MNT}/startup.nsh
        sudo mount ${TMP_LOOP_DEVICE}p2 ${ROOT_MNT}
        sudo mkdir -p ${ROOT_MNT}/{bin,sbin,usr,usr/bin,usr/sbin,etc,etc/init.d,lib,proc,sys,dev,run,var}
        sudo touch ${ROOT_MNT}/etc/fstab 
        sudo cp initramfs/bin/busybox ${ROOT_MNT}/bin
        sudo chroot ${ROOT_MNT} /bin/busybox --install -s
        sudo chmod 755 ${ROOT_MNT}/bin/busybox

        umount_disk_image mnt

        echo "Successfully mkfs"
        chmod 777 ${QEMU_DISK_IMAGE}
        echo "✅创建磁盘镜像完成"
    fi
    echo "磁盘镜像已经准备好"
}
write_disk_image() {
    echo "正在写入磁盘镜像..."
    mount_disk_image mnt

    sudo cp -r rootfs/* ${ROOT_MNT}

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