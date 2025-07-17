#define _GNU_SOURCE
#include "aliases.h"
#include "common.h"
#include "system_monitor.h"
#include <ncurses.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static NCursesMonitor *global_monitor = NULL;

static struct termios old_termios;

// signal handler for terminal size
void handle_resize(int sig) {
  (void)sig;
  if (global_monitor) {
    global_monitor->resize_flag = 1;
  }
  // Force ncurses to update its idea of terminal size
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    resizeterm(ws.ws_row, ws.ws_col);
  }
}

// function to recreate all windows with new terminal dimensions

void recreate_windows(NCursesMonitor *monitor) {
  // delete existings windows
  if (monitor->header_win)
    delwin(monitor->header_win);
  if (monitor->stats_win)
    delwin(monitor->stats_win);
  if (monitor->process_win)
    delwin(monitor->process_win);
  if (monitor->status_win)
    delwin(monitor->status_win);
  if (monitor->search_win)
    delwin(monitor->search_win);

  // Force ncurses to recognize the new terminal size
  endwin();
  refresh();
  clear();

  // get new terminal dimensions
  getmaxyx(stdscr, monitor->terminal_height, monitor->terminal_width);

  // Ensure minimum terminal size
  if (monitor->terminal_height < 15 || monitor->terminal_width < 60) {
    // Terminal too small, but still create basic windows
    monitor->header_win = newwin(1, monitor->terminal_width, 0, 0);
    monitor->stats_win = newwin(1, monitor->terminal_width, 1, 0);
    monitor->process_win =
        newwin(monitor->terminal_height - 4, monitor->terminal_width, 2, 0);
    monitor->status_win =
        newwin(2, monitor->terminal_width, monitor->terminal_height - 2, 0);
    monitor->search_win =
        newwin(1, monitor->terminal_width, monitor->terminal_height - 1, 0);
  } else {
    // Normal size layout
    monitor->header_win = newwin(3, monitor->terminal_width, 0, 0);
    monitor->stats_win = newwin(6, monitor->terminal_width, 3, 0);
    monitor->process_win =
        newwin(monitor->terminal_height - 11, monitor->terminal_width, 9, 0);
    monitor->status_win =
        newwin(2, monitor->terminal_width, monitor->terminal_height - 2, 0);
    monitor->search_win =
        newwin(1, monitor->terminal_width, monitor->terminal_height - 1, 0);
  }

  // Re-enable window settings
  scrollok(monitor->process_win, TRUE);
  keypad(monitor->process_win, TRUE);
  keypad(stdscr, TRUE);
  curs_set(0);

  // ensure selected process is still visible after resize
  int max_visible = getmaxy(monitor->process_win) - 3;
  if (max_visible > 0) {
    if (monitor->selected_process >=
        monitor->process_scroll_offset + max_visible) {
      monitor->process_scroll_offset =
          monitor->selected_process - max_visible + 1;
    }
    if (monitor->process_scroll_offset < 0) {
      monitor->process_scroll_offset = 0;
    }
  }

  // Force complete screen redraw
  clearok(stdscr, TRUE);
  refresh();

  // reset the resize flag
  monitor->resize_flag = 0;
}

int builtin_monitor(char **args) {
  SystemStats stats;
  ProcessInfo processes[500]; // Increased from 50 to 500 processes
  int proc_count;
  NCursesMonitor monitor;
  int refresh_rate = 1;

  if (args[1] != NULL && strcmp(args[1], "--help") == 0) {
    printf("monitor: Real-time system monitoring dashboard\n");
    printf("Usage: monitor [refresh_rate]\n");
    printf("Press 'q̈' to quit, 'r' to refresh, arrow keys to navigate\n");
    return 1;
  }

  if (args[1] != NULL) {
    refresh_rate = atoi(args[1]);
    if (refresh_rate < 1)
      refresh_rate = 1;
  }

  // init ncurses monitor
  if (!init_ncurses_monitor(&monitor)) {
    fprintf(stderr, "Failed to initialize ncurses monitor\n");
    return 1;
  }

  monitor.refresh_rate = refresh_rate;
  timeout(100); // Short timeout for responsive input, 100ms

  time_t last_update = 0;

  // Initial data load
  get_system_stats(&stats);
  proc_count = get_process_info(processes, 500);
  last_update = time(NULL);

  while (1) {
    // check for terminal resize
    if (monitor.resize_flag) {
      recreate_windows(&monitor);
    }
    time_t current_time = time(NULL);

    // Only update system stats at the specified refresh interval
    if (current_time - last_update >= refresh_rate) {
      get_system_stats(&stats);
      proc_count = get_process_info(processes, 500);
      last_update = current_time;
    }

    // Always update the display (for navigation highlighting)
    display_ncurses_dashboard(&monitor, &stats, processes, proc_count);

    int ch = getch();

    // Check for resize after input
    if (monitor.resize_flag) {
      recreate_windows(&monitor);
      continue; // Skip to next iteration to redraw everything
    }

    if (ch != ERR) {
      // Check if we're in search mode first - if so, let search handle ALL
      // input
      if (monitor.search_mode) {
        handle_monitor_input(&monitor, ch);
      } else {
        // Only process main UI commands when NOT in search mode
        if (ch == 'q' || ch == 'Q')
          break;
        if (ch == 'r' || ch == 'R') {
          // Force immediate refresh
          get_system_stats(&stats);
          proc_count = get_process_info(processes, 500);
          last_update = current_time;
          continue;
        }
        handle_monitor_input(&monitor, ch);
      }
    }
  }

  cleanup_ncurses_monitor(&monitor);
  return 1;
}

int init_ncurses_monitor(NCursesMonitor *monitor) {
  if (!monitor)
    return 0;

  memset(monitor, 0, sizeof(NCursesMonitor));

  // Initialize search
  monitor->search_mode = 0;
  monitor->search_buffer[0] = '\0';
  monitor->search_cursor = 0;
  monitor->resize_flag = 0;
  global_monitor = monitor;

  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  nodelay(stdscr, FALSE);
  curs_set(0);

  // enable colors
  if (has_colors()) {
    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_CYAN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_RED, COLOR_BLACK);
    init_pair(5, COLOR_WHITE, COLOR_BLACK);
  }

  // install signal handler for terminal resize
  signal(SIGWINCH, handle_resize);
  // get terminal dimensions

  getmaxyx(stdscr, monitor->terminal_height, monitor->terminal_width);

  // create windows

  monitor->header_win = newwin(3, monitor->terminal_width, 0, 0);
  monitor->stats_win = newwin(8, monitor->terminal_width, 3, 0);
  monitor->process_win =
      newwin(monitor->terminal_height - 13, monitor->terminal_width, 11, 0);
  monitor->status_win =
      newwin(2, monitor->terminal_width, monitor->terminal_height - 2, 0);

  if (!monitor->header_win || !monitor->stats_win || !monitor->process_win ||
      !monitor->status_win) {
    cleanup_ncurses_monitor(monitor);
    return 0;
  }
  // enable scrolling and keypad for all windows
  scrollok(monitor->process_win, TRUE);
  keypad(monitor->process_win, TRUE);

  return 1;
}

void cleanup_ncurses_monitor(NCursesMonitor *monitor) {
  if (!monitor)
    return;

  if (monitor->header_win)
    delwin(monitor->header_win);
  if (monitor->stats_win)
    delwin(monitor->stats_win);
  if (monitor->process_win)
    delwin(monitor->process_win);
  if (monitor->status_win)
    delwin(monitor->status_win);
  if (monitor->search_win)
    delwin(monitor->search_win);

  // Restore default signal handler
  signal(SIGWINCH, SIG_DFL);
  global_monitor = NULL;

  endwin();
}

void handle_monitor_input(NCursesMonitor *monitor, int ch) {
  if (!monitor)
    return;

  // Handle search mode input
  if (monitor->search_mode) {
    if (ch == 27) { // ESC key - clear search and exit
      monitor->search_mode = 0;
      monitor->search_buffer[0] = '\0';
      monitor->search_cursor = 0;
      monitor->selected_process = 0;
      monitor->process_scroll_offset = 0;
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8 || ch == 263) {
      // Backspace - remove character (try multiple key codes)
      if (monitor->search_cursor > 0) {
        monitor->search_cursor--;
        monitor->search_buffer[monitor->search_cursor] = '\0';
        monitor->selected_process = 0;
        monitor->process_scroll_offset = 0;
      }
    } else if (ch == KEY_ENTER || ch == '\n' || ch == '\r') {
      // Enter - apply search and exit search mode
      monitor->search_mode = 0;
    } else if (ch >= 32 && ch <= 126) {
      // Any printable character
      if (monitor->search_cursor < 254) {
        monitor->search_buffer[monitor->search_cursor] = (char)ch;
        monitor->search_cursor++;
        monitor->search_buffer[monitor->search_cursor] = '\0';
        monitor->selected_process = 0;
        monitor->process_scroll_offset = 0;
      }
    }
    return;
  }

  // Normal navigation mode
  int max_visible = getmaxy(monitor->process_win) - 3;

  switch (ch) {
  case '/':
    // Enter search mode
    monitor->search_mode = 1;
    monitor->search_buffer[0] = '\0';
    monitor->search_cursor = 0;
    monitor->selected_process = 0;
    monitor->process_scroll_offset = 0;
    break;

  case 27: // ESC key - clear search filter
    monitor->search_buffer[0] = '\0';
    monitor->search_cursor = 0;
    monitor->selected_process = 0;
    monitor->process_scroll_offset = 0;
    break;

  case KEY_UP:
  case 'k':
  case 'K':
    if (monitor->selected_process > 0) {
      monitor->selected_process--;
      if (monitor->selected_process < monitor->process_scroll_offset) {
        monitor->process_scroll_offset--;
      }
    }
    break;

  case KEY_DOWN:
  case 'j':
  case 'J':
    monitor->selected_process++;
    if (monitor->selected_process >=
        monitor->process_scroll_offset + max_visible) {
      monitor->process_scroll_offset++;
    }
    break;

  case KEY_HOME:
  case 'g':
    monitor->selected_process = 0;
    monitor->process_scroll_offset = 0;
    break;

  case KEY_END:
  case 'G':
    // Will be clamped in display function
    monitor->selected_process = 999999;
    break;
  }
}

void display_ncurses_dashboard(NCursesMonitor *monitor, SystemStats *stats,
                               ProcessInfo *processes, int proc_count) {
  if (!monitor || !stats || !processes)
    return;

  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char mem_used_str[32], mem_total_str[32];
  char disk_read_str[32], disk_write_str[32];
  char net_rx_str[32], net_tx_str[32];
  char gpu_used_str[32], gpu_total_str[32];

  // Format data
  float mem_percent = (float)stats->memory_used / stats->memory_total * 100;
  format_bytes(stats->memory_used, mem_used_str);
  format_bytes(stats->memory_total, mem_total_str);
  format_bytes(stats->disk_read, disk_read_str);
  format_bytes(stats->disk_write, disk_write_str);
  format_bytes(stats->net_rx, net_rx_str);
  format_bytes(stats->net_tx, net_tx_str);
  
  // Format GPU memory if available
  if (stats->gpu_memory_total > 0) {
    format_bytes(stats->gpu_memory_used, gpu_used_str);
    format_bytes(stats->gpu_memory_total, gpu_total_str);
  }

  // Clear all windows
  werase(monitor->header_win);
  werase(monitor->stats_win);
  werase(monitor->process_win);
  werase(monitor->status_win);

  // Draw header
  if (has_colors())
    wattron(monitor->header_win, COLOR_PAIR(1));
  box(monitor->header_win, 0, 0);
  mvwprintw(monitor->header_win, 1, (monitor->terminal_width - 24) / 2,
            "SYSTEM MONITOR DASHBOARD");
  mvwprintw(monitor->header_win, 2, (monitor->terminal_width - 19) / 2,
            "%02d:%02d:%02d %02d/%02d/%04d", tm_info->tm_hour, tm_info->tm_min,
            tm_info->tm_sec, tm_info->tm_mday, tm_info->tm_mon + 1,
            tm_info->tm_year + 1900);
  if (has_colors())
    wattroff(monitor->header_win, COLOR_PAIR(1));

  // Draw system stats
  box(monitor->stats_win, 0, 0);
  if (has_colors())
    wattron(monitor->stats_win, COLOR_PAIR(1));
  mvwprintw(monitor->stats_win, 0, 2, " System Statistics ");
  if (has_colors())
    wattroff(monitor->stats_win, COLOR_PAIR(1));

  // horizontal layout with wrapping

  int col1_width = monitor->terminal_width / 2 - 2;
  int col2_start = col1_width + 1;

  mvwprintw(monitor->stats_win, 1, 2, "CPU: %5.1f%%", stats->cpu_percent);
  if (stats->gpu_percent > 0 || stats->gpu_memory_total > 0) {
    mvwprintw(monitor->stats_win, 1, col2_start, "GPU: %5.1f%%",
              stats->gpu_percent);
  }

  // second row: Memory and GPU memory

  mvwprintw(monitor->stats_win, 2, 2, "MEM: %5.1f%% (%s/%s)", mem_percent,
            mem_used_str, mem_total_str);
  if (stats->gpu_memory_total > 0) {
    float gpu_mem_percent = (float)stats->gpu_memory_used / stats->gpu_memory_total * 100;
    mvwprintw(monitor->stats_win, 2, col2_start, "GPU MEM: %5.1f%% (%s/%s)",
              gpu_mem_percent, gpu_used_str, gpu_total_str);
  }
  
  // Third row: Disk I/O
  mvwprintw(monitor->stats_win, 3, 2, "DISK: R:%s W:%s", disk_read_str, disk_write_str);
  
  // Fourth row: Network I/O
  mvwprintw(monitor->stats_win, 4, 2, "NET: RX:%s TX:%s", net_rx_str, net_tx_str);

  // Filter processes based on search in real-time
  ProcessInfo *display_processes = processes;
  int display_count = proc_count;
  ProcessInfo *temp_processes = NULL;

  // Apply filter in real-time when search buffer has content (even during
  // typing)
  if (strlen(monitor->search_buffer) > 0) {
    temp_processes = malloc(proc_count * sizeof(ProcessInfo));
    display_count = 0;
    for (int i = 0; i < proc_count; i++) {
      if (strcasestr(processes[i].name, monitor->search_buffer) != NULL) {
        temp_processes[display_count] = processes[i];
        display_count++;
      }
    }
    display_processes = temp_processes;
  }

  // Draw process list
  box(monitor->process_win, 0, 0);
  if (has_colors())
    wattron(monitor->process_win, COLOR_PAIR(1));

  // Show search status in title when filter is applied (real-time during
  // search)
  if (strlen(monitor->search_buffer) > 0) {
    if (monitor->search_mode) {
      // Use bright yellow background for active search mode
      if (has_colors()) {
        wattron(monitor->process_win, COLOR_PAIR(3) | A_REVERSE | A_BOLD);
      }
      mvwprintw(monitor->process_win, 0, 2, " SEARCHING: '%s' (%d matches) ",
                monitor->search_buffer, display_count);
      if (has_colors()) {
        wattroff(monitor->process_win, COLOR_PAIR(3) | A_REVERSE | A_BOLD);
      }
    } else {
      mvwprintw(monitor->process_win, 0, 2, " Processes (filtered: %d) ",
                display_count);
    }
  } else {
    mvwprintw(monitor->process_win, 0, 2, " All Processes (by CPU) ");
  }

  mvwprintw(monitor->process_win, 1, 2,
            "PID    Name                     State  CPU%%    Memory");
  if (has_colors())
    wattroff(monitor->process_win, COLOR_PAIR(1));

  int max_visible = getmaxy(monitor->process_win) - 3;

  int end_index = monitor->process_scroll_offset + max_visible;
  if (end_index > display_count)
    end_index = display_count;

  // Clamp selected process to valid range
  if (monitor->selected_process >= display_count) {
    monitor->selected_process = display_count - 1;
    // Adjust scroll offset for end navigation
    monitor->process_scroll_offset = display_count - max_visible;
    if (monitor->process_scroll_offset < 0) {
      monitor->process_scroll_offset = 0;
    }
  }
  if (monitor->selected_process < 0) {
    monitor->selected_process = 0;
  }

  for (int i = monitor->process_scroll_offset; i < end_index; i++) {
    int line = i - monitor->process_scroll_offset + 2;
    char mem_str[32];
    format_bytes(display_processes[i].memory, mem_str);

    // Clear the line first
    wmove(monitor->process_win, line, 2);
    wclrtoeol(monitor->process_win);

    if (i == monitor->selected_process) {
      if (has_colors()) {
        wattron(monitor->process_win, COLOR_PAIR(5) | A_REVERSE);
      } else {
        wattron(monitor->process_win, A_REVERSE);
      }
    }

    mvwprintw(monitor->process_win, line, 2, "%-6d %-24s %-6c %6.1f%% %s",
              display_processes[i].pid, display_processes[i].name,
              display_processes[i].state, display_processes[i].cpu_percent,
              mem_str);

    if (i == monitor->selected_process) {
      if (has_colors()) {
        wattroff(monitor->process_win, COLOR_PAIR(5) | A_REVERSE);
      } else {
        wattroff(monitor->process_win, A_REVERSE);
      }
    }
  }

  // Free temporary filtered array
  if (temp_processes) {
    free(temp_processes);
  }

  // Draw status bar
  if (has_colors())
    wattron(monitor->status_win, COLOR_PAIR(1));

  if (monitor->search_mode) {
    // Use red background for search mode status to make it very obvious
    if (has_colors()) {
      wattron(monitor->status_win, COLOR_PAIR(4) | A_REVERSE | A_BOLD);
    }
    // Create search string with visible cursor
    char search_display[260];
    strncpy(search_display, monitor->search_buffer, monitor->search_cursor);
    search_display[monitor->search_cursor] = '\0';
    strcat(search_display, "_"); // Add cursor
    strcat(search_display, monitor->search_buffer + monitor->search_cursor);

    mvwprintw(monitor->status_win, 0, 2,
              "*** SEARCH MODE *** '%s' | %d matches | ESC=cancel Enter=apply",
              search_display, display_count);
    if (has_colors()) {
      wattroff(monitor->status_win, COLOR_PAIR(4) | A_REVERSE | A_BOLD);
      wattron(monitor->status_win, COLOR_PAIR(3) | A_BOLD);
    }
    mvwprintw(monitor->status_win, 1, 2, ">>> Type to filter processes... <<<");
    if (has_colors()) {
      wattroff(monitor->status_win, COLOR_PAIR(3) | A_BOLD);
    }
  } else {
    mvwprintw(monitor->status_win, 0, 2,
              "Press 'q' quit, 'r' refresh, '/' search, j/k arrows navigate, "
              "ESC clear");
    mvwprintw(monitor->status_win, 1, 2,
              "Refresh: %ds | Processes: %d/%d | Selected: %d | Scroll: %d",
              monitor->refresh_rate, display_count, proc_count,
              monitor->selected_process + 1, monitor->process_scroll_offset);
  }

  if (has_colors())
    wattroff(monitor->status_win, COLOR_PAIR(1));

  // Always refresh all windows
  wrefresh(monitor->header_win);
  wrefresh(monitor->stats_win);
  wrefresh(monitor->process_win);
  wrefresh(monitor->status_win);

  refresh();
}

void display_dashboard(SystemStats *stats, ProcessInfo *processes,
                       int proc_count) {
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

  pos +=
      sprintf(display_buffer + pos,
              "╔═══════════════════════════════════════════════════════════════"
              "═══════════════╗\n"
              "║                        SYSTEM MONITOR DASHBOARD               "
              "              ║\n"
              "║                        %02d:%02d:%02d %02d/%02d/%04d          "
              "                          ║\n"
              "╠═══════════════════════════════════════════════════════════════"
              "═══════════════╣\n",
              tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
              tm_info->tm_mday, tm_info->tm_mon + 1, tm_info->tm_year + 1900);

  // CPU Usage
  format_progress_bar((int)stats->cpu_percent, 40, progress_buf);
  pos += sprintf(display_buffer + pos, "║ CPU Usage: %s %5.1f%% ║\n",
                 progress_buf, stats->cpu_percent);

  // Memory Usage
  format_progress_bar((int)mem_percent, 40, progress_buf);
  pos += sprintf(display_buffer + pos, "║ Memory:    %s %5.1f%% ║\n",
                 progress_buf, mem_percent);
  pos += sprintf(display_buffer + pos,
                 "║            Used: %-15s / %-15s                   ║\n",
                 mem_used_str, mem_total_str);

  pos +=
      sprintf(display_buffer + pos,
              "║ Disk I/O:  Read:  %-20s                                   ║\n"
              "║            Write: %-20s                                   ║\n"
              "║ Network:   RX:    %-20s                                   ║\n"
              "║            TX:    %-20s                                   ║\n"
              "╠═══════════════════════════════════════════════════════════════"
              "═══════════════╣\n"
              "║                              TOP PROCESSES                    "
              "               ║\n"
              "╠═══════╦══════════════════════════════╦═══════╦══════════╦═════"
              "══════════════╣\n"
              "║  PID  ║           NAME               ║ STATE ║   CPU%%   ║    "
              "  MEMORY       ║\n"
              "╠═══════╬══════════════════════════════╬═══════╬══════════╬═════"
              "══════════════╣\n",
              disk_read_str, disk_write_str, net_rx_str, net_tx_str);

  for (int i = 0; i < proc_count && i < 10; i++) {
    format_bytes(processes[i].memory, buffer);
    pos += sprintf(display_buffer + pos,
                   "║ %5d ║ %-28s ║   %c   ║  %6.1f%% ║ %17s ║\n",
                   processes[i].pid, processes[i].name, processes[i].state,
                   processes[i].cpu_percent, buffer);
  }

  pos +=
      sprintf(display_buffer + pos, "╚═══════╩══════════════════════════════╩══"
                                    "═════╩══════════╩═══════════════════╝\n"
                                    "Press 'q' to quit, 'r' to refresh         "
                                    "                                     ");

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
    sscanf(buffer, "cpu %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &system,
           &idle, &iowait, &irq, &softirq);

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

  // gpu usage

  stats->gpu_percent = 0.0;
  stats->gpu_memory_used = 0;
  stats->gpu_memory_total = 0;

  FILE *gpu_file =
      popen("nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total "
            "--format=csv,noheader,nounits 2>/dev/null",
            "r");

  if (gpu_file) {
    char line[256];
    if (fgets(line, sizeof(line), gpu_file)) {
      sscanf(line, "%f, %lu, %lu", &stats->gpu_percent, &stats->gpu_memory_used,
             &stats->gpu_memory_total);
      stats->gpu_memory_used *= 1024 * 1024; // Convert MB to bytes
      stats->gpu_memory_total *= 1024 * 1024; // Convert MB to bytes
    }
    pclose(gpu_file);
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
      if (sscanf(buffer, "%*d %*d %31s %*d %*d %lu %*d %*d %*d %lu", device,
                 &r_sectors, &w_sectors) == 3) {
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
  if (!proc_dir)
    return 0;

  while ((entry = readdir(proc_dir)) != NULL && count < max_processes) {
    if (!isdigit(entry->d_name[0]))
      continue;

    int pid = atoi(entry->d_name);
    processes[count].pid = pid;
    processes[count].memory = 0;
    processes[count].cpu_percent = 0.0;
    processes[count].state = '?';
    strcpy(processes[count].name, "unknown");

    // Get process name and state from /proc/pid/stat
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    fp = fopen(path, "r");
    if (fp) {
      if (fgets(buffer, sizeof(buffer), fp)) {
        char *name_start = strchr(buffer, '(');
        char *name_end = strrchr(buffer, ')');
        if (name_start && name_end) {
          int name_len = name_end - name_start - 1;
          if (name_len > 255)
            name_len = 255;
          if (name_len > 0) {
            strncpy(processes[count].name, name_start + 1, name_len);
            processes[count].name[name_len] = '\0';
          }

          // Parse state (first field after the name)
          char *fields = name_end + 2;
          if (fields && *fields) {
            processes[count].state = *fields;
          }
        }
      }
      fclose(fp);
    }

    // Get memory usage from /proc/pid/statm (more reliable)
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);
    fp = fopen(path, "r");
    if (fp) {
      unsigned long size, resident, shared;
      if (fscanf(fp, "%lu %lu %lu", &size, &resident, &shared) >= 2) {
        // resident is in pages, convert to bytes (page size is typically 4096)
        processes[count].memory = resident * 4096;
      }
      fclose(fp);
    }

    // Simple CPU estimation - use a random value for demonstration
    // In a real implementation, you'd track CPU times over intervals
    processes[count].cpu_percent = (rand() % 100) / 10.0; // 0.0 to 9.9%

    count++;
  }

  closedir(proc_dir);

  // Sort by CPU usage (highest to lowest) to show most active processes first
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (processes[i].cpu_percent < processes[j].cpu_percent) {
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
      buffer[pos++] = '#'; // Use # instead of Unicode block
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
