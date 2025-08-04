#!/bin/bash
ARCH="x86_64"
QEMU=$(which qemu-system-${ARCH})

QEMU_DEVICES=""
QEMU_DISK_IMAGE="build/rootfs.img"
QEMU_SMP="2,cores=2,threads=1,sockets=1"
QEMU_MEMORY="256M"
QEMU_ARGUMENT=" "
QEMU_ARGUMENT+=" -smp ${QEMU_SMP} -m ${QEMU_MEMORY} "
APPEND_PARAMS=" console=ttyS0 quiet acpi=off "

# 设置无图形界面模式
QEMU_NOGRAPHIC=false
BIOS_TYPE=""
TAP_DEV="tap0"
CMDLINE="root=/dev/sda2 init=/init"

while true;do
    case "$1" in
        --display)
        case "$2" in
            window)
            QEMU_ARGUMENT+=" -vga virtio -display gtk "
            ;;
            nographic)
            QEMU_NOGRAPHIC=true
            QEMU_ARGUMENT+=" -nographic -serial mon:stdio "
            QEMU_ARGUMENT+=" -kernel vmlinuz -initrd build/initramfs.cpio.gz "
            ;;
            vnc)
            QEMU_ARGUMENT+=" -display vnc=:00 "
            ;;
        esac;shift 2;;
        --bios)
        case "$2" in
            uefi)
            BIOS_TYPE="uefi"
            ;;
            legacy)
            BIOS_TYPE="legacy"
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
        APPEND_PARAMS+=" ${CMDLINE} "
        shift 1;;
        *) break
      esac
  done

if [ ${BIOS_TYPE} == "uefi" ] ;then
    QEMU_ARGUMENT+=" -bios tools/arch/x86_64/EFI/OVMF-pure-efi.fd -boot order=d "
elif [ ${BIOS_TYPE} == "legacy" ] ;then
    QEMU_ARGUMENT+="  "
else
    echo "不支持的BIOS: ${BIOS_TYPE}"
fi
if [ ${QEMU_NOGRAPHIC} == true ]; then
    QEMU_ARGUMENT+=" -append '${APPEND_PARAMS}' "
fi
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
                sudo umount "$PART" #"${MOUNT_DIR}"
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

grub_cfg="
set timeout=7
set default=0
insmod efi_gop
insmod part_gpt fat ext2
insmod all_video
insmod gettext
set lang=zh_CN
loadfont unicode
insmod gfxterm gfxmenu
insmod png jpeg
terminal_output gfxterm
set gfxmode=auto
set theme0=(hd0,gpt1)/boot/grub/themes/vva/theme.txt
save_env theme0
function load_theme {
    load_env
    if [ -z \"\$theme_cur\" ]; then
        theme=\$theme0
    else
        theme=\$theme_cur
    fi
}
load_theme

menuentry 'Linux-minimal OS' --class gnu --class os {
    set root=(hd0,gpt2)
    linux /boot/vmlinuz ${CMDLINE} console=ttyS0 quiet acpi=off
    initrd /boot/initramfs.cpio.gz
}
menuentry 'Poweroff' --hotkey=p {
    halt
}
submenu 'Change Theme' --class themes --hotkey=t {
    load_theme
    menuentry 'Default Theme' {
        set theme_cur=(hd0,gpt1)/boot/grub/themes/default/theme.txt
        save_env theme_cur
        configfile (hd0,gpt1)/EFI/BOOT/grub.cfg
    }
    menuentry 'VVA Theme' {
        set theme_cur=(hd0,gpt1)/boot/grub/themes/vva/theme.txt
        save_env theme_cur
        configfile (hd0,gpt1)/EFI/BOOT/grub.cfg
    }
}
submenu 'More Options' --hotkey=m {
    load_theme
    menuentry 'UEFI Firmware Settings' --hotkey=u {
        fwsetup
    }
    menuentry 'Reboot' --class reset --hotkey=r {
        reboot
    }
}"

prepare_disk_image() {
    # 如果磁盘镜像不存在，则创建磁盘镜像
    echo "正在准备磁盘镜像..."
    if [ ! -f ${QEMU_DISK_IMAGE} ]; then
        echo "正在创建磁盘镜像..."
        qemu-img create -f raw ${QEMU_DISK_IMAGE} 128M || (echo "创建磁盘镜像失败" && exit 1)

        mount_disk_image
        echo "创建分区表..."
        sudo parted -s ${TMP_LOOP_DEVICE} mklabel gpt
        sudo parted -s ${TMP_LOOP_DEVICE} mkpart EFI fat32 2MiB 35MiB
        sudo parted -s ${TMP_LOOP_DEVICE} mkpart ROOT ext4 36MiB 100%
        sudo parted -s ${TMP_LOOP_DEVICE} set 1 esp on  # 设置EFI系统分区标志

        sudo partprobe ${TMP_LOOP_DEVICE}
        sudo udevadm settle --timeout=5
        # sudo blockdev --rereadpt ${TMP_LOOP_DEVICE}
        # sudo partx -v --update ${TMP_LOOP_DEVICE} || sudo partx -v --add ${TMP_LOOP_DEVICE}
        sleep 1

        echo "正在格式化磁盘镜像..."
        sudo mkfs.fat -F 32 -n "UEFI" ${TMP_LOOP_DEVICE}p1 || (echo "FAT32格式化失败")
        sudo mkfs.ext4 -F ${TMP_LOOP_DEVICE}p2 | grep "filesystem" || (echo "EXT4格式化失败")
        mkdir -p mnt ${EFI_MNT} ${ROOT_MNT}
        sudo mount ${TMP_LOOP_DEVICE}p1 ${EFI_MNT}
        sudo mkdir -p ${EFI_MNT}/EFI/BOOT ${EFI_MNT}/boot/
        sudo grub-install --target=x86_64-efi --efi-directory=${EFI_MNT} --boot-directory=${EFI_MNT}/boot --bootloader-id=GRUB --removable \
        --modules="part_gpt fat ext2 multiboot2 normal" --no-floppy --force ${TMP_LOOP_DEVICE}
        ls ${EFI_MNT}/EFI/BOOT
        # ROOT_UUID=$(sudo blkid -s PARTUUID -o value ${TMP_LOOP_DEVICE}p2)
        # grub_cfg=${grub_cfg//xxxxxx/$ROOT_UUID}
        sudo echo "${grub_cfg}" | sudo tee ${EFI_MNT}/EFI/BOOT/grub.cfg
        sudo mkdir -p ${EFI_MNT}/boot/grub/themes
        sudo cp -r tools/grub/themes/* ${EFI_MNT}/boot/grub/themes
        sudo mount ${TMP_LOOP_DEVICE}p2 ${ROOT_MNT}
        sudo mkdir -p ${ROOT_MNT}/{bin,sbin,usr,usr/bin,usr/sbin,etc,etc/init.d,lib,proc,sys,dev,run,var,boot}
        sudo touch ${ROOT_MNT}/etc/fstab
        sudo cp vmlinuz ${ROOT_MNT}/boot/
        sudo cp build/initramfs.cpio.gz ${ROOT_MNT}/boot/
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
    
    # tree -C -L 3 ${EFI_MNT}
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
