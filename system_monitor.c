#include "system_monitor.h"
#include "common.h"

static struct termios old_termios;

int builtin_monitor(char **args) {
    SystemStats stats;
    ProcessInfo processes[50];
    int proc_count;
    int refresh_rate = 1; // seconds
    
    if (args[1] != NULL && strcmp(args[1], "--help") == 0) {
        printf("monitor: Real-time system monitoring dashboard\n");
        printf("Usage: monitor [refresh_rate]\n");
        printf("Press 'q' to quit, 'r' to refresh immediately\n");
        return 1;
    }
    
    if (args[1] != NULL) {
        refresh_rate = atoi(args[1]);
        if (refresh_rate < 1) refresh_rate = 1;
    }
    
    // Setup terminal for raw input
    tcgetattr(STDIN_FILENO, &old_termios);
    struct termios new_termios = old_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
    
    // Make stdin non-blocking
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    
    // Enter alternative screen buffer
    printf("\033[?1049h");
    hide_cursor();
    fflush(stdout);
    
    while (1) {
        get_system_stats(&stats);
        proc_count = get_process_info(processes, 50);
        display_dashboard(&stats, processes, proc_count);
        
        // Check for user input
        if (kbhit()) {
            char c = getchar();
            if (c == 'q' || c == 'Q') break;
            if (c == 'r' || c == 'R') continue; // refresh immediately
        }
        
        sleep(refresh_rate);
    }
    
    // Restore terminal
    show_cursor();
    // Exit alternative screen buffer
    printf("\033[?1049l");
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
    fcntl(STDIN_FILENO, F_SETFL, flags);
    fflush(stdout);
    return 1;
}

void display_dashboard(SystemStats *stats, ProcessInfo *processes, int proc_count) {
    char buffer[256];
    char display_buffer[8192]; // Large buffer for entire display
    char progress_buf[50];
    char mem_used_str[32], mem_total_str[32];
    char disk_read_str[32], disk_write_str[32];
    char net_rx_str[32], net_tx_str[32];
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    // Format all the data first
    float mem_percent = (float)stats->memory_used / stats->memory_total * 100;
    format_bytes(stats->memory_used, mem_used_str);
    format_bytes(stats->memory_total, mem_total_str);
    format_bytes(stats->disk_read, disk_read_str);
    format_bytes(stats->disk_write, disk_write_str);
    format_bytes(stats->net_rx, net_rx_str);
    format_bytes(stats->net_tx, net_tx_str);
    
    // Build entire display in buffer
    int pos = sprintf(display_buffer, "\033[H"); // Move to home position
    
    pos += sprintf(display_buffer + pos,
        "╔══════════════════════════════════════════════════════════════════════════════╗\n"
        "║                        SYSTEM MONITOR DASHBOARD                             ║\n"
        "║                        %02d:%02d:%02d %02d/%02d/%04d                                    ║\n"
        "╠══════════════════════════════════════════════════════════════════════════════╣\n",
        tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
        tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year + 1900);
    
    // CPU Usage
    format_progress_bar((int)stats->cpu_percent, 40, progress_buf);
    pos += sprintf(display_buffer + pos, "║ CPU Usage: %s %5.1f%% ║\n", progress_buf, stats->cpu_percent);
    
    // Memory Usage
    format_progress_bar((int)mem_percent, 40, progress_buf);
    pos += sprintf(display_buffer + pos, "║ Memory:    %s %5.1f%% ║\n", progress_buf, mem_percent);
    pos += sprintf(display_buffer + pos, "║            Used: %-15s / %-15s                   ║\n", 
                   mem_used_str, mem_total_str);
    
    pos += sprintf(display_buffer + pos,
        "║ Disk I/O:  Read:  %-20s                                   ║\n"
        "║            Write: %-20s                                   ║\n"
        "║ Network:   RX:    %-20s                                   ║\n"
        "║            TX:    %-20s                                   ║\n"
        "╠══════════════════════════════════════════════════════════════════════════════╣\n"
        "║                              TOP PROCESSES                                   ║\n"
        "╠═══════╦══════════════════════════════╦═══════╦══════════╦═══════════════════╣\n"
        "║  PID  ║           NAME               ║ STATE ║   CPU%%   ║      MEMORY       ║\n"
        "╠═══════╬══════════════════════════════╬═══════╬══════════╬═══════════════════╣\n",
        disk_read_str, disk_write_str, net_rx_str, net_tx_str);
    
    for (int i = 0; i < proc_count && i < 10; i++) {
        format_bytes(processes[i].memory, buffer);
        pos += sprintf(display_buffer + pos, "║ %5d ║ %-28s ║   %c   ║  %6.1f%% ║ %17s ║\n",
                       processes[i].pid,
                       processes[i].name,
                       processes[i].state,
                       processes[i].cpu_percent,
                       buffer);
    }
    
    pos += sprintf(display_buffer + pos,
        "╚═══════╩══════════════════════════════╩═══════╩══════════╩═══════════════════╝\n"
        "Press 'q' to quit, 'r' to refresh                                              ");
    
    // Output entire buffer at once
    printf("%s", display_buffer);
    fflush(stdout);
}

void get_system_stats(SystemStats *stats) {
    FILE *fp;
    char buffer[1024];
    static unsigned long prev_idle = 0, prev_total = 0;
    static unsigned long prev_disk_read = 0, prev_disk_write = 0;
    static unsigned long prev_net_rx = 0, prev_net_tx = 0;
    
    // CPU Usage
    fp = fopen("/proc/stat", "r");
    if (fp) {
        fgets(buffer, sizeof(buffer), fp);
        unsigned long user, nice, system, idle, iowait, irq, softirq;
        sscanf(buffer, "cpu %lu %lu %lu %lu %lu %lu %lu",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq);
        
        unsigned long total = user + nice + system + idle + iowait + irq + softirq;
        unsigned long total_diff = total - prev_total;
        unsigned long idle_diff = idle - prev_idle;
        
        if (total_diff > 0) {
            stats->cpu_percent = 100.0 * (total_diff - idle_diff) / total_diff;
        } else {
            stats->cpu_percent = 0.0;
        }
        
        prev_total = total;
        prev_idle = idle;
        fclose(fp);
    }
    
    // Memory Usage - Read from /proc/meminfo for accuracy
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        unsigned long mem_total = 0, mem_free = 0, mem_available = 0;
        unsigned long buffers = 0, cached = 0;
        
        while (fgets(buffer, sizeof(buffer), fp)) {
            if (sscanf(buffer, "MemTotal: %lu kB", &mem_total) == 1) {
                stats->memory_total = mem_total * 1024;
            } else if (sscanf(buffer, "MemAvailable: %lu kB", &mem_available) == 1) {
                // MemAvailable accounts for buffers/cache that can be reclaimed
                stats->memory_used = (mem_total - mem_available) * 1024;
            } else if (sscanf(buffer, "MemFree: %lu kB", &mem_free) == 1) {
                // Fallback if MemAvailable is not available
                if (mem_available == 0) {
                    mem_free = mem_free;
                }
            } else if (sscanf(buffer, "Buffers: %lu kB", &buffers) == 1) {
                // For fallback calculation
            } else if (sscanf(buffer, "Cached: %lu kB", &cached) == 1) {
                // For fallback calculation
            }
        }
        
        // If MemAvailable wasn't found, calculate manually
        if (mem_available == 0 && mem_total > 0) {
            stats->memory_used = (mem_total - mem_free - buffers - cached) * 1024;
        }
        
        fclose(fp);
    }
    
    // Disk I/O
    fp = fopen("/proc/diskstats", "r");
    if (fp) {
        unsigned long read_sectors = 0, write_sectors = 0;
        while (fgets(buffer, sizeof(buffer), fp)) {
            unsigned long r_sectors, w_sectors;
            char device[32];
            if (sscanf(buffer, "%*d %*d %31s %*d %*d %lu %*d %*d %*d %lu",
                      device, &r_sectors, &w_sectors) == 3) {
                if (strncmp(device, "sd", 2) == 0 || strncmp(device, "nvme", 4) == 0) {
                    read_sectors += r_sectors;
                    write_sectors += w_sectors;
                }
            }
        }
        stats->disk_read = (read_sectors - prev_disk_read) * 512;
        stats->disk_write = (write_sectors - prev_disk_write) * 512;
        prev_disk_read = read_sectors;
        prev_disk_write = write_sectors;
        fclose(fp);
    }
    
    // Network I/O
    fp = fopen("/proc/net/dev", "r");
    if (fp) {
        fgets(buffer, sizeof(buffer), fp); // skip header
        fgets(buffer, sizeof(buffer), fp); // skip header
        
        unsigned long rx_bytes = 0, tx_bytes = 0;
        while (fgets(buffer, sizeof(buffer), fp)) {
            char interface[32];
            unsigned long rx, tx;
            if (sscanf(buffer, " %31[^:]: %lu %*d %*d %*d %*d %*d %*d %*d %lu",
                      interface, &rx, &tx) == 3) {
                if (strcmp(interface, "lo") != 0) { // skip loopback
                    rx_bytes += rx;
                    tx_bytes += tx;
                }
            }
        }
        stats->net_rx = rx_bytes - prev_net_rx;
        stats->net_tx = tx_bytes - prev_net_tx;
        prev_net_rx = rx_bytes;
        prev_net_tx = tx_bytes;
        fclose(fp);
    }
}

int get_process_info(ProcessInfo *processes, int max_processes) {
    DIR *proc_dir;
    struct dirent *entry;
    FILE *fp;
    char path[256];
    char buffer[1024];
    int count = 0;
    
    proc_dir = opendir("/proc");
    if (!proc_dir) return 0;
    
    while ((entry = readdir(proc_dir)) != NULL && count < max_processes) {
        if (!isdigit(entry->d_name[0])) continue;
        
        int pid = atoi(entry->d_name);
        processes[count].pid = pid;
        
        // Get process name and state
        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        fp = fopen(path, "r");
        if (fp) {
            if (fgets(buffer, sizeof(buffer), fp)) {
                char *name_start = strchr(buffer, '(');
                char *name_end = strrchr(buffer, ')');
                if (name_start && name_end) {
                    int name_len = name_end - name_start - 1;
                    if (name_len > 255) name_len = 255;
                    strncpy(processes[count].name, name_start + 1, name_len);
                    processes[count].name[name_len] = '\0';
                    
                    char *state_pos = name_end + 2;
                    processes[count].state = *state_pos;
                }
            }
            fclose(fp);
        }
        
        // Get memory usage
        snprintf(path, sizeof(path), "/proc/%d/status", pid);
        fp = fopen(path, "r");
        if (fp) {
            while (fgets(buffer, sizeof(buffer), fp)) {
                if (strncmp(buffer, "VmRSS:", 6) == 0) {
                    unsigned long mem_kb;
                    sscanf(buffer, "VmRSS: %lu kB", &mem_kb);
                    processes[count].memory = mem_kb * 1024;
                    break;
                }
            }
            fclose(fp);
        }
        
        processes[count].cpu_percent = 0.0; // Simplified for this example
        count++;
    }
    
    closedir(proc_dir);
    
    // Sort by memory usage (descending)
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (processes[i].memory < processes[j].memory) {
                ProcessInfo temp = processes[i];
                processes[i] = processes[j];
                processes[j] = temp;
            }
        }
    }
    
    return count;
}

void clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

void move_cursor(int row, int col) {
    printf("\033[%d;%dH", row, col);
    fflush(stdout);
}

void hide_cursor(void) {
    printf("\033[?25l");
    fflush(stdout);
}

void show_cursor(void) {
    printf("\033[?25h");
    fflush(stdout);
}

int kbhit(void) {
    int ch = getchar();
    if (ch != EOF) {
        ungetc(ch, stdin);
        return 1;
    }
    return 0;
}

void draw_progress_bar(int percentage, int width) {
    int filled = (percentage * width) / 100;
    printf("[");
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            printf("█");
        } else {
            printf(" ");
        }
    }
    printf("]");
}

void format_progress_bar(int percentage, int width, char *buffer) {
    int filled = (percentage * width) / 100;
    int pos = 0;
    buffer[pos++] = '[';
    for (int i = 0; i < width; i++) {
        if (i < filled) {
            buffer[pos++] = '#';  // Use # instead of Unicode block
        } else {
            buffer[pos++] = ' ';
        }
    }
    buffer[pos++] = ']';
    buffer[pos] = '\0';
}

void format_bytes(unsigned long bytes, char *buffer) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = bytes;
    
    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    
    if (unit == 0) {
        snprintf(buffer, 64, "%lu %s", bytes, units[unit]);
    } else {
        snprintf(buffer, 64, "%.1f %s", size, units[unit]);
    }
}