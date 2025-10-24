
#ifndef DIFF_VIEWER_H
#define DIFF_VIEWER_H

#include "common.h"
#include <termios.h>

#define MAX_FILES 100
#define MAX_FILENAME_LEN 256
#define MAX_DIFF_LINES 1000

typedef struct {
    char filename[MAX_FILENAME_LEN];
    char status; // 'M' = modified, 'A' = added, 'D' = deleted
} ChangedFile;

typedef struct {
    char line[512];
    char type; // '+' = addition, '-' = deletion, ' ' = context
    int line_number_old;
    int line_number_new;
} DiffLine;

typedef enum {
    MODE_FILE_LIST,
    MODE_FILE_CONTENT
} ViewMode;

typedef struct {
    ChangedFile files[MAX_FILES];
    int file_count;
    int selected_file;
    DiffLine diff_lines[MAX_DIFF_LINES];
    int diff_line_count;
    int diff_scroll_offset;
    int terminal_width;
    int terminal_height;
    int file_panel_width;
    ViewMode current_mode;
} DiffViewer;

int init_diff_viewer(DiffViewer *viewer);

int get_changed_files(DiffViewer *viewer);

int is_new_file(const char *filename);

int load_new_file_content(DiffViewer *viewer, const char *filename);

int load_file_diff(DiffViewer *viewer, const char *filename);

void render_diff_viewer(DiffViewer *viewer);

int handle_diff_input(DiffViewer *viewer, char key);

int run_diff_viewer(void);

void cleanup_diff_viewer(DiffViewer *viewer);

void get_terminal_size(int *width, int *height);

void set_raw_mode(struct termios *orig_termios);

void restore_terminal_mode(struct termios *orig_termios);

#endif // DIFF_VIEWER_H