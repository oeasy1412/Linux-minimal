#!/bin/bash
ARCH="x86_64"
QEMU=$(which qemu-system-${ARCH})

QEMU_DEVICES=""
QEMU_MACHINE=""
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
        QEMU_ARGUMENT+=" -device e1000,netdev=net0,mac=20:50:00:12:34:56 "
        QEMU_ARGUMENT+=" -netdev tap,id=net0,ifname=${TAP_DEV},vhost=on,script=no,downscript=no "
        # QEMU_ARGUMENT+=" -net nic -net tap,ifname=${TAP_DEV},vhost=on,script=no,downscript=no "
        APPEND_PARAMS+=" NET_MODE=bridge "
        shift 1;;
        --user)
        QEMU_ARGUMENT+=" -device e1000,netdev=hostnet1,id=net1 "
        QEMU_ARGUMENT+=" -netdev user,id=hostnet1,hostfwd=tcp::12580-:12580,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80,hostfwd=tcp::2233-:2233 "
        APPEND_PARAMS+=" NET_MODE=user "
        shift 1;;
        --usb)
        QEMU_ARGUMENT+=" -usb -device nec-usb-xhci,id=xhci -drive file=disk.qcow2,if=none,id=usbdisk -device usb-storage,drive=usbdisk,bus=xhci.0 "
        shift 1;;
        *) break
      esac
  done

QEMU_ARGUMENT+=" -append '${APPEND_PARAMS}' "
QEMU_ARGUMENT+=" -D ../qemu.log "

sh -c "${QEMU} ${QEMU_ARGUMENT}"