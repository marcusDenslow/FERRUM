/**
 * diff_viewer.h
 * Interactive diff viewer for git changes
 */

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

/**
 * Initialize the diff viewer
 */
int init_diff_viewer(DiffViewer *viewer);

/**
 * Get list of changed files from git
 */
int get_changed_files(DiffViewer *viewer);

/**
 * Check if a file is a new untracked file
 */
int is_new_file(const char *filename);

/**
 * Load content of a new file and show all lines as additions
 */
int load_new_file_content(DiffViewer *viewer, const char *filename);

/**
 * Load diff for a specific file
 */
int load_file_diff(DiffViewer *viewer, const char *filename);

/**
 * Render the diff viewer interface
 */
void render_diff_viewer(DiffViewer *viewer);

/**
 * Handle keyboard input for navigation
 */
int handle_diff_input(DiffViewer *viewer, char key);

/**
 * Run the interactive diff viewer
 */
int run_diff_viewer(void);

/**
 * Clean up diff viewer resources
 */
void cleanup_diff_viewer(DiffViewer *viewer);

/**
 * Get terminal size
 */
void get_terminal_size(int *width, int *height);

/**
 * Set terminal to raw mode for interactive input
 */
void set_raw_mode(struct termios *orig_termios);

/**
 * Restore terminal to original mode
 */
void restore_terminal_mode(struct termios *orig_termios);

#endif // DIFF_VIEWER_H