[bits 16]        ; 16位实模式
[org 0x7c00]     ; BIOS 加载 MBR 到 0x7c00

start:
    ; 设置段寄存器
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00 ; 设置栈指针

    ; 清屏
    mov ax, 0x0003
    int 0x10

    ; 打印欢迎消息
    mov si, msg
    call print_string

    ; 加载第二阶段引导程序
    mov ah, 0x42   ; 使用扩展读取
    mov dl, 0x80
    mov si, disk_packet
    int 0x13       ; 调用BIOS磁盘服务
    jc disk_error  ; 如果出错跳转

    ; 跳转到第二阶段
    jmp 0x0000:0x7e00

disk_error:
    ; 显示错误代码
    mov al, ah
    add al, '0'
    mov ah, 0x0e
    int 0x10
    mov si, error_msg
    call print_string
    jmp $

; 打印字符串函数
print_string:
    lodsb            ; 从SI加载字节到AL
    or al, al        ; AL=0?
    jz done          ; 是则结束
    mov ah, 0x0e     ; BIOS打印字符功能
    int 0x10         ; 调用BIOS视频服务
    jmp print_string
done:
    ret

; 数据
msg db 'Stage1: MRB Loading...', 0
error_msg db 'Disk read error!', 0

disk_packet:
    db 0x10        ; 包大小
    db 0           ; 保留
    dw 1           ; 要读取的扇区数
    dw 0x7e00      ; 目标偏移
    dw 0           ; 目标段
    dq 34          ; LBA 起始扇区

; 填充剩余空间并添加MBR签名
times 510-($-$$) db 0
dw 0xaa55