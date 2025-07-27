#include <algorithm>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <pwd.h>
#include <sstream>
#include <string>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

using namespace std;

// 进程信息结构体
struct ProcessInfo {
    char state;
    string user;
    pid_t pid;
    pid_t ppid;
    float cpu_percent;
    int pri;
    int ni;
    long vsize;        // 虚拟内存(KB)
    long rss;          // 物理内存(KB)
    int psr;           // 进程运行的 CPU 核心编号
    long _starttime;   // 进程启动时钟滴答数
    time_t _start_sec; // 进程启动绝对时间(s)
    string stime;      // 进程启动时间
    string wchan;
    string tty;
    string time;
    string cmd;
};

// 获取系统启动时间 (秒)
time_t getSystemBootTime() {
    static time_t boot_time = 0;
    if (boot_time == 0) {
        ifstream uptime_file("/proc/stat");
        if (!uptime_file) {
            return -1;
        }
        string line;
        while (getline(uptime_file, line)) {
            if (line.find("btime") != string::npos) {
                stringstream ss(line);
                string key;
                ss >> key >> boot_time;
                break;
            }
        }
    }
    return boot_time;
}

string cur_tty;
string getCurrentTerminal() {
    char* tty = ttyname(STDIN_FILENO);
    if (tty != nullptr) {
        string tty_str(tty);
        // 提取tty信息 (如 "/dev/pts/0" -> "pts/0")
        size_t pos = tty_str.find('/', 2);
        if (pos != string::npos) {
            return tty_str.substr(pos + 1);
        }
        return tty_str;
    }
    cerr << "Error: Could not get current tty!\n";
    return "";
}

// 解析 /proc/[PID]/stat
bool parseProcStat(pid_t pid, ProcessInfo& info) {
    string path = "/proc/" + to_string(pid) + "/stat";
    ifstream file(path);
    if (!file) {
        return false;
    }
    string line;
    getline(file, line);
    istringstream ss(line);
    // 提取统计信息
    vector<string> tokens;
    string token;
    while (ss >> token) {
        tokens.push_back(token);
    }
    // 应有至少 41 个字段
    if (tokens.size() < 41) {
        return false;
    }
    try {
        info.pid = stoi(tokens[0]);
        info.cmd = tokens[1].substr(1, tokens[1].size() - 2); // 去掉括号
        if (tokens[2].size() == 1) {
            info.state = tokens[2][0]; // 进程状态
        } else {
            info.state = '?';
        }
        info.ppid = stoi(tokens[3]);
        int rt_pri = stoi(tokens[17]);
        info.pri = 100 - rt_pri;
        info.ni = stoi(tokens[18]);
        info._starttime = stol(tokens[21]);
        info.psr = stoi(tokens[38]);
        // 计算进程启动绝对时间(s)
        time_t boot_time = getSystemBootTime();
        long clk_tck = sysconf(_SC_CLK_TCK);
        if (clk_tck > 0) {
            info._start_sec = boot_time + info._starttime / clk_tck;
            // 格式化启动时间
            struct tm* tm_info = localtime(&info._start_sec);
            char buffer[6];
            strftime(buffer, sizeof(buffer), "%H:%M", tm_info);
            info.stime = buffer;
        } else {
            info.stime = "?";
        }
        // 计算 CPU 时间
        unsigned long utime = stoul(tokens[13]);
        unsigned long stime = stoul(tokens[14]);
        unsigned long total_time = utime + stime;
        // 转换格式
        unsigned long seconds = total_time / clk_tck;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        seconds %= 60, minutes %= 60;
        ostringstream time_ss;
        time_ss << setfill('0') << setw(2) << hours << ":" << setfill('0') << setw(2) << minutes << ":" << setfill('0')
                << setw(2) << seconds;
        info.time = time_ss.str();
        // 终端设备号
        long tty_nr = stol(tokens[6]);
        // 检查是否为控制终端 (tty_major = 4, 5, 136, 137)
        if (tty_nr > 0) {
            unsigned major = (tty_nr >> 8) & 0xFF;
            unsigned minor = tty_nr & 0xFF;
            ostringstream tty_ss;
            if (major == 4) {
                if (minor <= 63) {
                    tty_ss << "tty" << minor;
                } else if (minor <= 255) {
                    tty_ss << "ttyS" << (minor - 64);
                } else {
                    tty_ss << "tty?" << minor;
                }
            } else if (major == 5) {
                tty_ss << "pts/" << minor;
            } else if (major == 136 || major == 137) {
                tty_ss << "pts/" << minor;
            } else if (major == 3) {
                tty_ss << "tty" << minor;
            } else {
                tty_ss << "?";
            }
            info.tty = tty_ss.str();
        } else {
            info.tty = "?"; // 无控制终端
        }
        // unsigned long wchan_addr = stoul(tokens[35]);
        // 尝试读取 wchan 文件
        string wchan_path = "/proc/" + to_string(pid) + "/wchan";
        ifstream wchan_file(wchan_path);
        string wchan_line;
        if (wchan_file && getline(wchan_file, wchan_line)) {
            if (wchan_line != "0") {
                // 截断到合适的长度
                info.wchan = wchan_line.substr(0, 6);
            } else {
                info.wchan = "-";
            }
        } else {
            info.wchan = "?";
        }
    } catch (...) {
        return false;
    }

    return true;
}

// 解析 /proc/[PID]/status
bool parseProcStatus(pid_t pid, ProcessInfo& info) {
    string path = "/proc/" + to_string(pid) + "/status";
    ifstream file(path);
    if (!file) {
        return false;
    }
    string line;
    while (getline(file, line)) {
        if (line.find("Uid:") == 0) {
            istringstream ss(line);
            string key, uid_str;
            ss >> key >> uid_str;
            // 转换为用户名
            try {
                uid_t uid = stoi(uid_str);
                struct passwd* pwd = getpwuid(uid);
                info.user = pwd ? string(pwd->pw_name) : to_string(uid);
            } catch (...) {
                info.user = "?";
            }
        } else if (line.find("VmSize:") == 0) {
            // 解析内存大小 (kB)
            size_t pos = line.find(":");
            if (pos != string::npos) {
                try {
                    string size_str = line.substr(pos + 1);
                    size_str.erase(remove_if(size_str.begin(), size_str.end(), ::isspace), size_str.end());
                    size_str.erase(size_str.size() - 2); // 移除 " kB"
                    info.vsize = stol(size_str);
                } catch (...) {
                    info.vsize = -1;
                }
            }
        } else if (line.find("VmRSS:") == 0) {
            size_t pos = line.find(":");
            if (pos != string::npos) {
                try {
                    string size_str = line.substr(pos + 1);
                    size_str.erase(remove_if(size_str.begin(), size_str.end(), ::isspace), size_str.end());
                    size_str.erase(size_str.size() - 2); // 移除 " kB"
                    info.rss = stol(size_str);
                } catch (...) {
                    info.rss = -1;
                }
            }
        }
    }

    return true;
}

// 解析 /proc/[PID]/cmdline
void parseProcCmdline(pid_t pid, ProcessInfo& info) {
    string path = "/proc/" + to_string(pid) + "/cmdline";
    ifstream file(path);
    if (!file) {
        return;
    }
    string cmdline;
    getline(file, cmdline);
    // 替换空字符为空格
    replace(cmdline.begin(), cmdline.end(), '\0', ' ');
    // 移除尾部的多余空格
    if (!cmdline.empty() && cmdline.back() == ' ') {
        cmdline.pop_back();
    }
    // 如果命令行非空则更新
    if (!cmdline.empty()) {
        info.cmd = cmdline;
    }
}

// 获取所有进程信息
vector<ProcessInfo> getAllProcesses() {
    vector<ProcessInfo> processes;
    DIR* dir = opendir("/proc");
    if (!dir) {
        perror("opendir");
        return processes;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // 只检查数字命名的目录
        if (entry->d_type != DT_DIR) {
            continue;
        }
        string name(entry->d_name);
        if (name.find_first_not_of("0123456789") != string::npos) {
            continue;
        }
        try {
            pid_t pid = stoi(name);
            ProcessInfo info;
            // 解析进程信息
            if (parseProcStat(pid, info)) {
                parseProcStatus(pid, info);
                parseProcCmdline(pid, info);
                processes.push_back(info);
            }
        } catch (...) {
            continue;
        }
    }
    closedir(dir);
    return processes;
}

// 默认格式输出
void printDefault(const vector<ProcessInfo>& processes, bool every) {
    cout << "    PID TTY          TIME CMD\n";
    for (const auto& p : processes) {
        if (!every && p.tty != cur_tty) {
            continue;
        }
        string cmd_first = p.cmd.substr(0, p.cmd.find(' '));
        cout << setw(7) << p.pid << " " << setw(8) << left << p.tty.substr(0, 8) << right << " " << setw(8) << p.time
             << " " << cmd_first << "\n";
    }
}
// -l
void printLong(const vector<ProcessInfo>& processes, bool every) {
    cout << "F S   UID     PID    PPID  C PRI  NI ADDR SZ WCHAN  TTY          TIME CMD\n";
    for (const auto& p : processes) {
        if (!every && p.tty != cur_tty) {
            continue;
        }
        string cmd_first = p.cmd.substr(0, p.cmd.find(' '));
        cout << "0 " << p.state << " " << setw(5) << p.user.substr(0, 5) << " " // UID 截断到5字符
             << setw(7) << p.pid << " " << setw(7) << p.ppid << " "
             << " C " // CPU % 占位
             << setw(3) << p.pri << " " << setw(3) << p.ni << " "
             << "- " // ADDR
                     //  << setw(5) << (p.vsize > 0 ? to_string(p.vsize / 4096) : "?") // 转换为MB
             << setw(5) << (p.vsize > 0 ? to_string(p.vsize / 4) : "?") << "  " // KB
             << setw(6) << p.wchan << " "                                       // WCHAN
             << p.tty << " " << setw(12) << p.time << " " << cmd_first << "\n";
    }
}
// -f
void printFull(const vector<ProcessInfo>& processes, bool every) {
    cout << "F S        UID     PID    PPID  C PRI  NI ADDR SZ   RSS PSR STIME WCHAN  TTY          TIME CMD\n";
    for (const auto& p : processes) {
        if (!every && p.tty != cur_tty) {
            continue;
        }
        cout << "0 " << p.state << " " << setw(10) << p.user.substr(0, 10) << " "       // UID 截断到10字符
             << setw(7) << p.pid << " " << setw(7) << p.ppid << " " << setw(2) << " C " // CPU % 占位
             << setw(3) << p.pri << " " << setw(3) << p.ni << " "
             << "- " // ADDR
                     //  << setw(5) << (p.vsize > 0 ? to_string(p.vsize / 4096) : "?") << " "          // 转换为MB
             << setw(5) << (p.vsize > 0 ? to_string(p.vsize / 4) : "?") << " "                    // KB
             << setw(5) << (p.rss > 0 ? to_string(p.rss) : "?") << " "                            // KB
             << setw(3) << p.psr << " " << setw(5) << p.stime << " " << setw(6) << p.wchan << " " // WCHAN
             << p.tty << " " << setw(12) << p.time << " " << p.cmd << "\n";
    }
}

int main(int argc, char* argv[]) {
    // 获取所有进程信息
    auto processes = getAllProcesses();
    cur_tty = getCurrentTerminal();
    // 按PID排序
    sort(processes.begin(), processes.end(), [](const ProcessInfo& a, const ProcessInfo& b) { return a.pid < b.pid; });
    bool opt_l = false, opt_e = false, opt_f = false;
    int opt;
    while ((opt = getopt(argc, argv, "leAf")) != -1) {
        switch (opt) {
        case 'l':
            opt_l = true;
            break;
        case 'e':
        case 'A':
            opt_e = true;
            break;
        case 'f':
            opt_f = true;
            break;
        default:
            cerr << "\nUsage: " << argv[0] << " [options]\n";
            cerr << "\nBasic Options:\n"
                 << "  -l      Long format\n"
                 << "  -e, -A  All processes (including other users)\n"
                 << "  -f      Extra full\n";
            return 1;
        }
    }
    if (opt_f) {
        printFull(processes, opt_e);
    } else if (opt_l) {
        printLong(processes, opt_e);
    } else {
        printDefault(processes, opt_e);
    }
    return 0;
}