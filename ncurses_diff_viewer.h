/**
 * ncurses_diff_viewer.h
 * NCurses-based interactive diff viewer for git changes
 */

#ifndef NCURSES_DIFF_VIEWER_H
#define NCURSES_DIFF_VIEWER_H

#include "common.h"
#include <ncurses.h>

#define MAX_FILES 100
#define MAX_FILENAME_LEN 256
#define MAX_FULL_FILE_LINES 2000
#define MAX_COMMITS 50
#define MAX_COMMIT_TITLE_LEN 256
#define MAX_AUTHOR_INITIALS 3

typedef struct {
    char filename[MAX_FILENAME_LEN];
    char status; // 'M' = modified, 'A' = added, 'D' = deleted
    int marked_for_commit; // 1 if marked for commit, 0 otherwise
} NCursesChangedFile;

typedef struct {
    char line[1024];
    char type; // '+' = addition, '-' = deletion, ' ' = context, '@' = hunk header
    int is_diff_line; // 1 if this is a diff line, 0 if original file line
} NCursesFileLine;

typedef struct {
    char hash[16]; // Short commit hash
    char author_initials[MAX_AUTHOR_INITIALS];
    char title[MAX_COMMIT_TITLE_LEN];
    int is_pushed; // 1 if pushed to remote, 0 if local only
} NCursesCommit;

typedef enum {
    NCURSES_MODE_FILE_LIST,
    NCURSES_MODE_FILE_VIEW,
    NCURSES_MODE_COMMIT_LIST
} NCursesViewMode;

typedef enum {
    SYNC_STATUS_IDLE,
    SYNC_STATUS_SYNCING_APPEARING,
    SYNC_STATUS_SYNCING_VISIBLE,
    SYNC_STATUS_SYNCING_DISAPPEARING,
    SYNC_STATUS_PUSHING_APPEARING,
    SYNC_STATUS_PUSHING_VISIBLE,
    SYNC_STATUS_PUSHING_DISAPPEARING,
    SYNC_STATUS_SYNCED_APPEARING,
    SYNC_STATUS_SYNCED_VISIBLE,
    SYNC_STATUS_SYNCED_DISAPPEARING
} SyncStatus;

typedef struct {
    NCursesChangedFile files[MAX_FILES];
    int file_count;
    int selected_file;
    NCursesFileLine file_lines[MAX_FULL_FILE_LINES];
    int file_line_count;
    int file_scroll_offset;
    NCursesCommit commits[MAX_COMMITS];
    int commit_count;
    int selected_commit;
    WINDOW *file_list_win;
    WINDOW *file_content_win;
    WINDOW *commit_list_win;
    WINDOW *status_bar_win;
    int terminal_width;
    int terminal_height;
    int file_panel_width;
    int file_panel_height;
    int commit_panel_height;
    int status_bar_height;
    NCursesViewMode current_mode;
    SyncStatus sync_status;
    int spinner_frame;
    time_t last_sync_time;
    int animation_frame;
    int text_char_count;
} NCursesDiffViewer;

/**
 * Initialize the ncurses diff viewer
 */
int init_ncurses_diff_viewer(NCursesDiffViewer *viewer);

/**
 * Get list of changed files from git
 */
int get_ncurses_changed_files(NCursesDiffViewer *viewer);

/**
 * Load full file content with diff highlighting
 */
int load_full_file_with_diff(NCursesDiffViewer *viewer, const char *filename);

/**
 * Render the file list window
 */
void render_file_list_window(NCursesDiffViewer *viewer);

/**
 * Render the file content window
 */
void render_file_content_window(NCursesDiffViewer *viewer);

/**
 * Handle keyboard input for navigation
 */
int handle_ncurses_diff_input(NCursesDiffViewer *viewer, int key);

/**
 * Run the ncurses diff viewer
 */
int run_ncurses_diff_viewer(void);

/**
 * Clean up ncurses diff viewer resources
 */
void cleanup_ncurses_diff_viewer(NCursesDiffViewer *viewer);

/**
 * Create temporary file with current working version
 */
int create_temp_file_with_changes(const char *filename, char *temp_path);

/**
 * Create temporary file with git HEAD version
 */
int create_temp_file_git_version(const char *filename, char *temp_path);

/**
 * Get commit history
 */
int get_commit_history(NCursesDiffViewer *viewer);

/**
 * Toggle file marking for commit
 */
void toggle_file_mark(NCursesDiffViewer *viewer, int file_index);

/**
 * Mark all files for commit
 */
void mark_all_files(NCursesDiffViewer *viewer);

/**
 * Commit marked files with title and message
 */
int commit_marked_files(NCursesDiffViewer *viewer, const char *commit_title, const char *commit_message);

/**
 * Push specific commit
 */
int push_commit(NCursesDiffViewer *viewer, int commit_index);

/**
 * Pull commits from remote
 */
int pull_commits(NCursesDiffViewer *viewer);

/**
 * Render the commit list window
 */
void render_commit_list_window(NCursesDiffViewer *viewer);

/**
 * Get commit title and message input from user
 */
int get_commit_title_input(char *title, int max_len, char *message, int max_message_len);

/**
 * Draw a box with rounded corners
 */
void draw_rounded_box(WINDOW *win);

/**
 * Render the status bar
 */
void render_status_bar(NCursesDiffViewer *viewer);

/**
 * Update sync status and check for new files
 */
void update_sync_status(NCursesDiffViewer *viewer);

#endif // NCURSES_DIFF_VIEWER_H