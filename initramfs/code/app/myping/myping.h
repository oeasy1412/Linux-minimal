#ifndef _PING_H_
#define _PING_H_

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <getopt.h>
#include <iostream>
#include <limits>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <signal.h>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

struct ICMPPacket {
    struct icmphdr header;
    char data[56]; // 默认值为 56

    ICMPPacket(int pid, int sequence);
    ~ICMPPacket() = default;
    void calc_checksum();
    inline size_t total_size() const { return sizeof(header) + sizeof(data); }
    inline timeval* timestamp() { return reinterpret_cast<timeval*>(data); }
    inline char* payload() { return data + sizeof(timeval); }
    inline size_t payload_size() const { return sizeof(data) - sizeof(timeval); }
};

class PingStatistics {
  private:
    int packets_sent = 0;
    int packets_recv = 0;
    double total_time = 0.0;
    double min_time = std::numeric_limits<double>::max();
    double max_time = 0;

  public:
    PingStatistics() = default;
    ~PingStatistics() = default;
    inline void update_response_time(double time) {
        total_time += time;
        min_time = std::min(min_time, time);
        max_time = std::max(max_time, time);
    }
    inline void increment_sent() { packets_sent++; }
    inline void increment_received() { packets_recv++; }

    inline int packets_lost() const { return packets_sent - packets_recv; }
    inline double loss_rate() const {
        return (packets_sent > 0) ? (static_cast<double>(packets_lost()) / packets_sent) * 100.0 : 0.0;
    };
    inline double average_time() const { return (packets_recv > 0) ? total_time / packets_recv : 0.0; };
    inline void print_statistics(const std::string& target_ip) const {
        printf("\n--- %s ping statistics ---\n", target_ip.c_str());
        printf("%d packets transmitted, %d packets received, %.0f%% packet loss\n",
               packets_sent,
               packets_recv,
               loss_rate());

        if (packets_recv > 0) {
            printf("round-trip min/avg/max = %.3f/%.3f/%.3f ms\n", min_time, average_time(), max_time);
        }
    }
};

class PingClient {
  private:
    std::string target_ip;
    struct sockaddr_in dest_addr;
    char ip_str[INET_ADDRSTRLEN];

    int pid;
    int socket_fd;
    int count;
    int interval;
    int ttl;
    double timeout;
    bool quiet_mode;
    bool running;
    PingStatistics stats;

    bool create_socket();
    bool setup_target_address();
    void close_socket();
    static double calc_time_diff(const struct timeval& start, const struct timeval& end);
    bool send_ping_packet(int sequence);
    bool receive_ping_reply(int sequence);

  public:
    PingClient(const std::string& target_ip = "None",
               int count = 4,
               double timeout = 1000.0,
               int ttl = 64,
               bool quiet_mode = false,
               int interval = 1);
    ~PingClient();

    bool initialize();
    void run();
    inline void stop() { running = false; }

    bool parse_arguments(int argc, char* argv[]);
    static void print_usage(const char* prog_name);
};

#endif // _PING_H_