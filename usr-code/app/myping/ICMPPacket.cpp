#include "myping.h"

ICMPPacket::ICMPPacket(int pid, int sequence) {
    header.type = ICMP_ECHO; // Type 8 回显请求(Echo Request)
    header.code = 0;
    header.checksum = 0;                     // 检验和
    header.un.echo.id = htons(pid & 0xFFFF); // htons()将主机字节序转换为`网络字节序`：标记报文
    header.un.echo.sequence = htons(sequence);

    struct timeval tv;
    gettimeofday(&tv, nullptr);
    timestamp()->tv_sec = htonl(static_cast<uint32_t>(tv.tv_sec));
    timestamp()->tv_usec = htonl(static_cast<uint32_t>(tv.tv_usec));
    // 填充剩余数据区（标准ping模式）
    char* data_ptr = payload();
    for (size_t i = 0; i < payload_size(); ++i) {
        data_ptr[i] = static_cast<char>(i);
    }
    calc_checksum();
}

// 待检验部分开始，每 16bits 进行一次回卷的加法计算（如果最后剩8位左移8位再加），然后进行一次反码运算，就得到检验和
void ICMPPacket::calc_checksum() {
    char buffer[total_size()];
    memcpy(buffer, &header, sizeof(header));
    memcpy(buffer + sizeof(header), data, sizeof(data));
    size_t len = sizeof(buffer);

    uint32_t sum = 0;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(buffer);
    for (size_t i = 0; i < len - 1; i += 2) {
        sum += (bytes[i] << 8) | bytes[i + 1];
    }
    if (len & 1) {
        sum += bytes[len - 1] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16); // 实现回卷
    }
    header.checksum = htons(~sum); // 取反
}