#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef struct {
  float cpu_percent;
  float gpu_percent;
  unsigned long gpu_memory_used;
  unsigned long gpu_memory_total;
  unsigned long memory_used;
  unsigned long memory_total;
  unsigned long disk_read;
  unsigned long disk_write;
  unsigned long net_rx;
  unsigned long net_tx;
  int process_count;
} SystemStats;

typedef struct {
  WINDOW *main_win;
  WINDOW *header_win;
  WINDOW *stats_win;
  WINDOW *process_win;
  WINDOW *status_win;
  WINDOW *search_win;
  int terminal_height;
  int terminal_width;
  int selected_process;
  int refresh_rate;
  int process_scroll_offset;
  int search_mode;
  char search_buffer[256];
  int search_cursor;
  volatile sig_atomic_t resize_flag;
} NCursesMonitor;

typedef struct {
  int pid;
  char name[256];
  float cpu_percent;
  unsigned long memory;
  char state;
} ProcessInfo;

int init_ncurses_monitor(NCursesMonitor *monitor);
int builtin_monitor(char **args);
void display_dashboard(SystemStats *stats, ProcessInfo *processes,
                       int proc_count);
void get_system_stats(SystemStats *stats);
int get_process_info(ProcessInfo *processes, int max_processes);
void clear_screen(void);
void move_cursor(int row, int col);
void hide_cursor(void);
void show_cursor(void);
int kbhit(void);
void draw_progress_bar(int percentage, int width);
void format_progress_bar(int percentage, int width, char *buffer);
void format_bytes(unsigned long bytes, char *buffer);
void cleanup_ncurses_monitor(NCursesMonitor *monitor);
void display_ncurses_dashboard(NCursesMonitor *monitor, SystemStats *stats,
                               ProcessInfo *processes, int proc_count);
void handle_monitor_input(NCursesMonitor *monitor, int ch);

#endif
