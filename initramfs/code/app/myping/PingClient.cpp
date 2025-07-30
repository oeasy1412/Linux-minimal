#include "myping.h"

#include <netdb.h>

PingClient::PingClient(const std::string& target_ip, int count, double timeout, int ttl, bool quiet_mode, int interval)
    : target_ip(target_ip),
      pid(getpid()),
      socket_fd(-1),
      count(count),
      interval(interval),
      ttl(ttl),
      timeout(timeout),
      quiet_mode(quiet_mode),
      running(false) {
    memset(&dest_addr, 0, sizeof(dest_addr));
}

PingClient::~PingClient() { close_socket(); }

bool PingClient::create_socket() {
    // 尝试使用非特权 ICMP 套接字
    // #if defined(__linux__)
    //     socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    //     if (socket_fd >= 0) {
    //         return true;
    //     }
    //     if (errno == EPROTONOSUPPORT) {
    //         printf("内核不支持 SOCK_DGRAM ICMP, 使用原始模式\n");
    //     } else {
    //         perror("Warning: SOCK_DGRAM ICMP 创建失败");
    //     }
    // #endif
    // 回退到原始套接字
    socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (socket_fd < 0) {
        perror("套接字创建失败");
        return false;
    }
    // 设置TTL
    if (setsockopt(socket_fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) != 0) {
        perror("设置TTL失败");
    }
    // 启用IP头
    // #if defined(__linux__)
    //     int opt = 1;
    //     if (setsockopt(socket_fd, IPPROTO_IP, IP_HDRINCL, &opt, sizeof(opt)) < 0) {
    //         perror("设置 IP_HDRINCL 失败");
    //     }
    // #endif
    // printf("使用原始模式(SOCK_RAW)套接字\n");
    return true;
}

void PingClient::close_socket() {
    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }
}

bool PingClient::setup_target_address() {
    struct hostent* he = gethostbyname(target_ip.c_str());
    if (!he) {
        perror("域名解析失败");
        return false;
    }
    struct in_addr** addr_list = (struct in_addr**)he->h_addr_list;
    if (!addr_list[0]) { // 获取第一个IPv4地址
        fprintf(stderr, "没有找到有效的IP地址\n");
        return false;
    }
    strcpy(ip_str, inet_ntoa(*addr_list[0]));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr = *addr_list[0];
    // // 字符串IP转二进制格式
    // if (inet_pton(AF_INET, target_ip.c_str(), &dest_addr.sin_addr) <= 0) { // IPv4地址族
    //     if (errno == 0) {
    //         fprintf(stderr, "Invalid IP address: %s\n", target_ip.c_str());
    //     } else {
    //         perror("inet_pton failed");
    //     }
    //     return false;
    // }
    return true;
}

double PingClient::calc_time_diff(const struct timeval& start, const struct timeval& end) {
    return static_cast<double>(end.tv_sec - start.tv_sec) * 1000.0 +
           static_cast<double>(end.tv_usec - start.tv_usec) / 1000.0;
}

bool PingClient::send_ping_packet(int sequence) {
    ICMPPacket packet(pid, sequence);
    struct timeval send_time;
    gettimeofday(&send_time, nullptr);
    // 发送到目标地址（UDP风格）
    ssize_t bytes_sent = sendto(socket_fd, &packet, sizeof(packet), 0, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (bytes_sent < 0) {
        perror("Failed to send ICMP packet");
        return false;
    }
    stats.increment_sent();
    return true;
}

bool PingClient::receive_ping_reply(int sequence) {
    char recv_buf[1024];
    struct sockaddr_in recv_addr; // 响应来源地址
    socklen_t addr_len = sizeof(recv_addr);

    // 超时循环机制
    struct timeval start_time;
    gettimeofday(&start_time, nullptr);
    double remaining_timeout = timeout;
    while (remaining_timeout > 0) {
        struct timeval cur_time;
        gettimeofday(&cur_time, nullptr);
        double elapsed = calc_time_diff(start_time, cur_time);
        remaining_timeout = timeout - elapsed;
        if (remaining_timeout <= 0) {
            printf("break\n");
            break;
        }
        // // 设置select超时
        // select_timeout.tv_sec = static_cast<time_t>(remaining_timeout / 1000);
        // select_timeout.tv_usec = static_cast<suseconds_t>((remaining_timeout - select_timeout.tv_sec * 1000) * 1000);
        // // 使用select等待可读事件
        // fd_set read_fds;
        // FD_ZERO(&read_fds);
        // FD_SET(socket_fd, &read_fds);
        // int ready = select(socket_fd + 1, &read_fds, nullptr, nullptr, &select_timeout);
        // 设置poll超时
        struct pollfd pfd;
        pfd.fd = socket_fd;
        pfd.events = POLLIN;
        int ready = poll(&pfd, 1, static_cast<int>(remaining_timeout));

        if (ready == 0) {
            break;
        } else if (ready < 0) {
            if (errno == EINTR)
                continue;
            perror("poll 错误");
            break;
        }
        // 接收数据
        ssize_t bytes_received =
            recvfrom(socket_fd, recv_buf, sizeof(recv_buf), MSG_DONTWAIT, (struct sockaddr*)&recv_addr, &addr_len);
        if (bytes_received <= 0) {
            if (errno != EINTR) {
                perror("recvfrom 错误");
            }
            continue;
        }
        // 解析IP头
        unsigned int ip_header_len = 0;
        if (bytes_received >= (ssize_t)sizeof(struct ip)) {
            struct ip* ip_reply = (struct ip*)recv_buf;
            ip_header_len = ip_reply->ip_hl * 4;
            if (ip_header_len == 0) {
                ip_header_len = sizeof(struct ip);
            }
        }
        if (bytes_received < static_cast<ssize_t>(ip_header_len + sizeof(struct icmphdr) + sizeof(timeval))) {
            continue;
        }
        // 解析ICMP头
        struct icmphdr* icmp_reply = (struct icmphdr*)(recv_buf + ip_header_len);
        uint16_t recv_id = ntohs(icmp_reply->un.echo.id);
        uint16_t recv_seq = ntohs(icmp_reply->un.echo.sequence);
        // 过滤非匹配包
        if (icmp_reply->type != ICMP_ECHOREPLY || recv_id != pid || recv_seq != sequence) {
            continue;
        }
        // if (icmp_reply->type == ICMP_ECHO) {
        //     // printf("过滤自己发出的请求包\n");
        //     continue;
        // }
        // if (icmp_reply->type != ICMP_ECHOREPLY) {
        //     printf("非回复包: type=%d ", icmp_reply->type);
        // }
        // if (recv_id != pid) {
        //     printf("ID不匹配: recv_id=%d, pid=%d ", recv_id, pid);
        // }
        // if (recv_seq != sequence) {
        //     printf("序列号不匹配: recv_seq=%d, expected=%d ", recv_seq, sequence);
        // }
        // 提取时间戳
        char* icmp_data_ = recv_buf + ip_header_len + sizeof(struct icmphdr);
        timeval* net_timestamp = reinterpret_cast<timeval*>(icmp_data_);
        struct timeval sent_time;
        sent_time.tv_sec = ntohl(net_timestamp->tv_sec);
        sent_time.tv_usec = ntohl(net_timestamp->tv_usec);
        // 计算延迟
        struct timeval end_time;
        gettimeofday(&end_time, nullptr);
        struct timeval send_time;
        if (bytes_received >= sizeof(struct ip) + sizeof(struct icmphdr) + sizeof(struct timeval)) {
            // 直接从数据区拷贝时间戳
            memcpy(&send_time, recv_buf + sizeof(struct ip) + sizeof(struct icmphdr), sizeof(struct timeval));
        } else {
            send_time = end_time;
        }
        double delay = calc_time_diff(sent_time, end_time);
        // 更新统计信息
        stats.increment_received();
        stats.update_response_time(delay);
        if (!quiet_mode) {
            printf("%zd bytes from %s: icmp_seq=%d ttl=%d time=%.3f ms\n",
                   bytes_received - ip_header_len,
                   inet_ntoa(recv_addr.sin_addr),
                   recv_seq,
                   ttl,
                   delay);
        }
        return true;
    }
    if (!quiet_mode) {
        printf("Request timeout for icmp_seq=%d\n", sequence);
    }
    return false;
}

bool PingClient::initialize() {
    if (!create_socket()) {
        return false;
    }
    if (!setup_target_address()) {
        close_socket();
        return false;
    }
    running = true;
    return true;
}

void PingClient::run() {
    if (!running) {
        fprintf(stderr, "PingClient not initialized\n");
        return;
    }

    printf("PING %s (%s): %d(%d) bytes of data.\n", target_ip.c_str(), ip_str, 56, 56 + 20 + 8); // TODO

    for (int i = 1; i <= count && running; ++i) {
        if (send_ping_packet(i)) {
            receive_ping_reply(i);
        }
        if (i < count && running) {
            sleep(interval);
        }
    }
    stats.print_statistics(target_ip);
}

bool PingClient::parse_arguments(int argc, char* argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "c:i:W:t:qh")) != EOF) {
        switch (opt) {
        case 'c': // 发送次数
            if (!optarg) {
                fprintf(stderr, "Option -c requires an argument\n");
                return false;
            }
            count = atoi(optarg);
            if (count <= 0) {
                fprintf(stderr, "Count must be a positive integer\n");
                return false;
            }
            break;
        case 'i': // 发送间隔
            if (!optarg) {
                fprintf(stderr, "Option -i requires an argument\n");
                return false;
            }
            interval = atoi(optarg);
            if (interval < 0) {
                fprintf(stderr, "Interval must be a positive integer\n");
                return false;
            }
            break;
        case 'W': // 超时时间
            if (!optarg) {
                fprintf(stderr, "Option -W requires an argument\n");
                return false;
            }
            timeout = atof(optarg);
            if (timeout <= 0) {
                fprintf(stderr, "Timeout must be a positive number\n");
                return false;
            }
            break;
        case 't': // TTL 选项
            if (!optarg) {
                fprintf(stderr, "Option -t requires an argument\n");
                return false;
            }
            ttl = atoi(optarg);
            if (ttl <= 0 || ttl > 255) {
                fprintf(stderr, "TTL must be between 1 and 255\n");
                return false;
            }
            break;
        case 'q': // 静默模式
            quiet_mode = true;
            break;
        case 'h': // 帮助信息
            print_usage(argv[0]);
            return false;
        default:
            fprintf(stderr, "Unknown option: %s\n", argv[optind - 1]);
            print_usage(argv[0]);
            return false;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "Error: Destination IP address required\n");
        print_usage(argv[0]);
        return false;
    }
    target_ip = argv[optind];
    return true;
}

void PingClient::print_usage(const char* prog_name) {
    printf("Usage: %s [options] <destination>\n", prog_name);
    printf("Send ICMP ECHO_REQUEST packets to network hosts.\n");
    printf("Options:\n");
    printf("  -c <count>         stop after <count> replies (default: 4)\n");
    printf("  -i <interval>      seconds between sending each packet (default: 1)\n");
    printf("  -W <timeout>       time to wait for response (default: 1000ms)\n");
    printf("  -t <ttl>           define time to live (default: 64)\n");
    printf("  -q                 quiet output\n");
    printf("  -h                 print help and exit\n");
}