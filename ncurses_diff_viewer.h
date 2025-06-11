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
#define MAX_STASHES 20
#define MAX_BRANCHES 5
#define MAX_BRANCHNAME_LEN 256

typedef struct {
  char stash_info[512];
} NCursesStash;

typedef struct {
  char filename[MAX_FILENAME_LEN];
  char status;           // 'M' = modified, 'A' = added, 'D' = deleted
  int marked_for_commit; // 1 if marked for commit, 0 otherwise
} NCursesChangedFile;

typedef struct {
  char name[MAX_BRANCHNAME_LEN];
  int status;
  int commits_ahead;
  int commits_behind;
} NCursesBranches;

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
  NCURSES_MODE_COMMIT_LIST,
  NCURSES_MODE_COMMIT_VIEW,
  NCURSES_MODE_STASH_LIST,
  NCURSES_MODE_STASH_VIEW,
  NCURSES_MODE_BRANCH_LIST,
  NCURSES_MODE_BRANCH_VIEW
} NCursesViewMode;

typedef enum {
  SYNC_STATUS_IDLE,
  SYNC_STATUS_SYNCING_APPEARING,
  SYNC_STATUS_SYNCING_VISIBLE,
  SYNC_STATUS_SYNCING_DISAPPEARING,
  SYNC_STATUS_PUSHING_APPEARING,
  SYNC_STATUS_PUSHING_VISIBLE,
  SYNC_STATUS_PUSHING_DISAPPEARING,
  SYNC_STATUS_PULLING_APPEARING,
  SYNC_STATUS_PULLING_VISIBLE,
  SYNC_STATUS_PULLING_DISAPPEARING,
  SYNC_STATUS_SYNCED_APPEARING,
  SYNC_STATUS_SYNCED_VISIBLE,
  SYNC_STATUS_SYNCED_DISAPPEARING,
  SYNC_STATUS_PUSHED_APPEARING,
  SYNC_STATUS_PUSHED_VISIBLE,
  SYNC_STATUS_PUSHED_DISAPPEARING,
  SYNC_STATUS_PULLED_APPEARING,
  SYNC_STATUS_PULLED_VISIBLE,
  SYNC_STATUS_PULLED_DISAPPEARING
} SyncStatus;

typedef struct {
  NCursesChangedFile files[MAX_FILES];
  int file_count;
  int selected_file;
  NCursesFileLine file_lines[MAX_FULL_FILE_LINES];
  int file_line_count;
  int file_scroll_offset;
  int file_cursor_line;
  NCursesCommit commits[MAX_COMMITS];
  int commit_count;
  int selected_commit;
  NCursesStash stashes[MAX_STASHES];
  NCursesBranches branches[MAX_BRANCHES];
  int stash_count;
  int branch_count;
  int selected_stash;
  int selected_branch;
  WINDOW *file_list_win;
  WINDOW *file_content_win;
  WINDOW *commit_list_win;
  WINDOW *stash_list_win;
  WINDOW *branch_list_win;
  WINDOW *status_bar_win;
  int terminal_width;
  int terminal_height;
  int file_panel_width;
  int file_panel_height;
  int commit_panel_height;
  int stash_panel_height;
  int branch_panel_height;
  int status_bar_height;
  NCursesViewMode current_mode;
  SyncStatus sync_status;
  int spinner_frame;
  time_t last_sync_time;
  int animation_frame;
  int text_char_count;
  int pushing_branch_index;
  int pulling_branch_index;
  SyncStatus branch_push_status;
  SyncStatus branch_pull_status;
  int branch_animation_frame;
  int branch_text_char_count;
  int critical_operation_in_progress; // Prevent fetching during critical ops

  // Background fetch management
  pid_t fetch_pid;       // Process ID of background fetch
  int fetch_in_progress; // Flag to track if fetch is running

  // Branch-specific commits for hover functionality
  char branch_commits[MAX_COMMITS][2048]; // Larger buffer for formatted commits
  int branch_commit_count;
  char current_branch_for_commits[MAX_BRANCHNAME_LEN];
  int branch_commits_scroll_offset;
  int branch_commits_cursor_line;

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
int commit_marked_files(NCursesDiffViewer *viewer, const char *commit_title,
                        const char *commit_message);

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
int get_commit_title_input(char *title, int max_len, char *message,
                           int max_message_len);

/**
 * Draw a box with rounded corners
 */
void draw_rounded_box(WINDOW *win);

/**
 * Render the status bar
 */
void render_status_bar(NCursesDiffViewer *viewer);

void render_branch_list_window(NCursesDiffViewer *viewer);

/**
 * Update sync status and check for new files
 */
void update_sync_status(NCursesDiffViewer *viewer);

int get_ncurses_git_stashes(NCursesDiffViewer *viewer);

int get_ncurses_git_branches(NCursesDiffViewer *viewer);

typedef enum {
  DELETE_LOCAL = 0,
  DELETE_REMOTE = 1,
  DELETE_BOTH = 2,
  DELETE_CANCEL = 3
} DeleteBranchOption;

int get_branch_name_input(char *branch_name, int max_len);

int create_git_branch(const char *branch_name);

int get_rename_branch_input(const char *current_name, char *new_name,
                            int max_len);

int rename_git_branch(const char *old_name, const char *new_name);

int show_delete_branch_dialog(const char *branch_name);

void show_error_popup(const char *error_message);

int get_git_remotes(char remotes[][256], int max_remotes);

int show_upstream_selection_dialog(const char *branch_name,
                                   char *upstream_result, int max_len);

int get_current_branch_name(char *branch_name, int max_len);

int branch_has_upstream(const char *branch_name);

int delete_git_branch(const char *branch_name, DeleteBranchOption option);

int create_ncurses_git_stash(NCursesDiffViewer *viewer);

int get_stash_name_input(char *stash_name, int max_len);

void render_stash_list_window(NCursesDiffViewer *viewer);

/**
 * Load commit details for viewing
 */
int load_commit_for_viewing(NCursesDiffViewer *viewer, const char *commit_hash);

/**
 * Load stash details for viewing
 */
int load_stash_for_viewing(NCursesDiffViewer *viewer, int stash_index);

/**
 * Load commits for a specific branch for the hover preview
 */
int load_branch_commits(NCursesDiffViewer *viewer, const char *branch_name);

/**
 * Parse branch commits into navigable lines for branch view mode
 */
int parse_branch_commits_to_lines(NCursesDiffViewer *viewer);

/**
 * Start background fetch process
 */
void start_background_fetch(NCursesDiffViewer *viewer);

/**
 * Check if background fetch is complete and update UI accordingly
 */
void check_background_fetch(NCursesDiffViewer *viewer);

/**
 * Move cursor up/down while skipping empty lines
 */
void move_cursor_smart(NCursesDiffViewer *viewer, int direction);

int has_staged_files(NCursesDiffViewer *viewer);

#endif // NCURSES_DIFF_VIEWER_H
