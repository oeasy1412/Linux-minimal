#include "myping.h"

PingClient* g_ping_instance = nullptr;
void signal_handler(int signum) {
    if (signum == SIGINT && g_ping_instance) {
        g_ping_instance->stop();
    }
}

int main(int argc, char* argv[]) {
    PingClient ping_client;
    if (!ping_client.parse_arguments(argc, argv)) {
        return 1;
    }
    g_ping_instance = &ping_client;
    if (!ping_client.initialize()) { // sudo setcap cap_net_raw+ep ./myping
        return 1;
    }
    signal(SIGINT, signal_handler);
    ping_client.run();

    return 0;
}