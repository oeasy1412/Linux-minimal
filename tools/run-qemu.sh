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
QEMU_ARGUMENT+=" -append 'console=ttyS0 quiet acpi=off' "

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
        QEMU_ARGUMENT+=" -append 'nokaslr' "
        shift 1;;
        --bridge)
        QEMU_ARGUMENT+=" -device e1000,netdev=net0,mac=20:50:00:12:34:56 "
        QEMU_ARGUMENT+=" -netdev tap,id=net0,ifname=${TAP_DEV},vhost=on,script=no,downscript=no "
        # QEMU_ARGUMENT+=" -net nic -net tap,ifname=${TAP_DEV},vhost=on,script=no,downscript=no "
        shift 1;;
        *) break
      esac
  done

QEMU_ARGUMENT+=" -D ../qemu.log "

sh -c "${QEMU} ${QEMU_ARGUMENT}"