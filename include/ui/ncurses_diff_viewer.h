#ifndef NCURSES_DIFF_VIEWER_H
#define NCURSES_DIFF_VIEWER_H

#include "common.h"
#include <ncurses.h>

#define MAX_FILES 100
#define MAX_FILENAME_LEN 256
#define MAX_FULL_FILE_LINES 10000
#define MAX_COMMITS 1000
#define MAX_COMMIT_TITLE_LEN 256
#define MAX_AUTHOR_INITIALS 3
#define MAX_STASHES 100
#define MAX_BRANCHES 5
#define MAX_BRANCHNAME_LEN 256

typedef struct {
  char stash_info[512];
} NCursesStash;

typedef struct {
  char filename[MAX_FILENAME_LEN];
  char status;           // 'M' = modified, 'A' = added, 'D' = deleted
  int marked_for_commit; // 1 if marked for commit, 0 otherwise
  int has_staged_changes;
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
  int hunk_id;
  int is_staged;
  int line_number_old;
  int line_number_new;
  int is_context;
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
  NCursesCommit *commits;
  int commit_count;
  int commit_capacity;
  int selected_commit;
  int commit_scroll_offset;
  NCursesStash stashes[MAX_STASHES];
  NCursesBranches branches[MAX_BRANCHES];
  int stash_count;
  int branch_count;
  int selected_stash;
  int stash_scroll_offset;
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

  int split_view_mode;         // 0 = normal view, 1 = split staged/unstaged
  int staged_scroll_offset;    // Scroll position for staged pane
  int active_pane;             // 0 = unstaged, 1 = staged
  char current_file_path[512]; // Path of currently viewed file
  int total_hunks;             // Total number of hunks in current file
  NCursesFileLine
      staged_lines[MAX_FULL_FILE_LINES]; // Separate storage for staged content
  int staged_line_count;                 // Number of lines in staged view
  int staged_cursor_line;

  // Fuzzy search state
  int fuzzy_search_active;      // 1 if fuzzy search is active
  char fuzzy_search_query[256]; // Current search query
  int fuzzy_search_query_len;   // Length of current query

  // Scored search results
  struct {
    int file_index;
    int score;
  } fuzzy_scored_files[MAX_FILES]; // Scored and sorted results

  int fuzzy_filtered_count; // Number of filtered files
  int fuzzy_selected_index; // Currently selected in fuzzy list
  int fuzzy_scroll_offset;  // Scroll position in fuzzy list
  WINDOW *fuzzy_input_win;  // Input window for search
  WINDOW *fuzzy_list_win;   // Results list window

  // State tracking to prevent unnecessary redraws
  int fuzzy_needs_full_redraw; // 1 if full redraw needed (borders, input, list)
  int fuzzy_needs_input_redraw;  // 1 if input field needs redraw
  int fuzzy_needs_list_redraw;   // 1 if file list needs redraw
  char fuzzy_last_query[256];    // Last rendered query
  int fuzzy_last_selected;       // Last rendered selection
  int fuzzy_last_scroll;         // Last rendered scroll position
  int fuzzy_last_filtered_count; // Last rendered result count

  // Generic grep search state (for commits, stashes, branches)
  int grep_search_active;           // 1 if grep search is active
  NCursesViewMode grep_search_mode; // Which mode grep is searching in
  char grep_search_query[256];      // Current search query
  int grep_search_query_len;        // Length of current query

  // Scored grep search results
  struct {
    int item_index;
    int score;
  } grep_scored_items[MAX_COMMITS]; // Scored and sorted results (reuse
                                    // MAX_COMMITS)

  int grep_filtered_count;  // Number of filtered items
  int grep_selected_index;  // Currently selected in grep list
  int grep_scroll_offset;   // Scroll position in grep list
  WINDOW *grep_input_win;   // Input window for search
  WINDOW *grep_list_win;    // Results list window
  WINDOW *grep_preview_win; // Grep search preview

  // State tracking for grep search rendering
  int grep_needs_full_redraw;   // 1 if full redraw needed
  int grep_needs_input_redraw;  // 1 if input field needs redraw
  int grep_needs_list_redraw;   // 1 if item list needs redraw
  char grep_last_query[256];    // Last rendered query
  int grep_last_selected;       // Last rendered selection
  int grep_last_scroll;         // Last rendered scroll position
  int grep_last_filtered_count; // Last rendered result count

} NCursesDiffViewer;

int init_ncurses_diff_viewer(NCursesDiffViewer *viewer);

int get_ncurses_changed_files(NCursesDiffViewer *viewer);

int load_full_file_with_diff(NCursesDiffViewer *viewer, const char *filename);

void render_file_list_window(NCursesDiffViewer *viewer);

void render_file_content_window(NCursesDiffViewer *viewer);

int handle_ncurses_diff_input(NCursesDiffViewer *viewer, int key);

int run_ncurses_diff_viewer(void);

void cleanup_ncurses_diff_viewer(NCursesDiffViewer *viewer);

int create_temp_file_with_changes(const char *filename, char *temp_path);

int create_temp_file_git_version(const char *filename, char *temp_path);

int get_commit_history(NCursesDiffViewer *viewer);

void toggle_file_mark(NCursesDiffViewer *viewer, int file_index);

void mark_all_files(NCursesDiffViewer *viewer);

int commit_marked_files(NCursesDiffViewer *viewer, const char *commit_title,
                        const char *commit_message);

int push_commit(NCursesDiffViewer *viewer, int commit_index);

int pull_commits(NCursesDiffViewer *viewer);

void render_commit_list_window(NCursesDiffViewer *viewer);

int get_commit_title_input(char *title, int max_len, char *message,
                           int max_message_len);

void draw_rounded_box(WINDOW *win);

void render_status_bar(NCursesDiffViewer *viewer);

void render_branch_list_window(NCursesDiffViewer *viewer);

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

int load_commit_for_viewing(NCursesDiffViewer *viewer, const char *commit_hash);

int load_stash_for_viewing(NCursesDiffViewer *viewer, int stash_index);

int load_branch_commits(NCursesDiffViewer *viewer, const char *branch_name);

int parse_branch_commits_to_lines(NCursesDiffViewer *viewer);

void start_background_fetch(NCursesDiffViewer *viewer);

void check_background_fetch(NCursesDiffViewer *viewer);

void move_cursor_smart(NCursesDiffViewer *viewer, int direction);

void move_cursor_smart_unstaged(NCursesDiffViewer *viewer, int direction);

void move_cursor_smart_staged(NCursesDiffViewer *viewer, int direction);

int wrap_line_to_width(const char *input_line, char wrapped_lines[][1024],
                       int max_lines, int width);

int render_wrapped_line(WINDOW *win, const char *line, int start_y, int start_x,
                        int width, int max_rows, int color_pair, int reverse);

int calculate_wrapped_line_height(const char *line, int width);

int load_file_preview(NCursesDiffViewer *viewer, const char *filename);

void update_preview_for_current_selection(NCursesDiffViewer *viewer);

int get_single_input(const char *title, const char *prompt, char *input,
                     int input_len, int is_password);

int get_github_credentials(char *username, int username_len, char *token,
                           int token_len);

int execute_git_with_auth(const char *base_cmd, const char *username,
                          const char *token);

int stage_hunk_by_line(NCursesDiffViewer *viewer, int line_index);

int load_file_with_staging_info(NCursesDiffViewer *viewer,
                                const char *filename);

void rebuild_staged_view(NCursesDiffViewer *viewer);

int apply_staged_changes(NCursesDiffViewer *viewer);

int reset_staged_changes(NCursesDiffViewer *viewer);

int unstage_line_from_git(NCursesDiffViewer *viewer, int staged_line_index);

int commit_staged_changes_only(NCursesDiffViewer *viewer,
                               const char *commit_title,
                               const char *commit_message);

void rebuild_staged_view_from_git(NCursesDiffViewer *viewer);

void init_fuzzy_search(NCursesDiffViewer *viewer);

void cleanup_fuzzy_search(NCursesDiffViewer *viewer);

void enter_fuzzy_search_mode(NCursesDiffViewer *viewer);

void exit_fuzzy_search_mode(NCursesDiffViewer *viewer);

void update_fuzzy_filter(NCursesDiffViewer *viewer);

void render_fuzzy_search(NCursesDiffViewer *viewer);

void render_fuzzy_input(NCursesDiffViewer *viewer);

void render_fuzzy_list_content(NCursesDiffViewer *viewer);

void create_fuzzy_windows_with_borders(NCursesDiffViewer *viewer);

int handle_fuzzy_search_input(NCursesDiffViewer *viewer, int key);

void select_fuzzy_file(NCursesDiffViewer *viewer);

void init_grep_search(NCursesDiffViewer *viewer);

void cleanup_grep_search(NCursesDiffViewer *viewer);

void enter_grep_search_mode(NCursesDiffViewer *viewer);

void exit_grep_search_mode(NCursesDiffViewer *viewer);

void update_grep_filter(NCursesDiffViewer *viewer);

void render_grep_search(NCursesDiffViewer *viewer);

int handle_grep_search_input(NCursesDiffViewer *viewer, int key);

void select_grep_item(NCursesDiffViewer *viewer);

int calculate_grep_score(const char *pattern, const char *text);

void extract_branch_from_stash(const char *stash_info, char *branch_name,
                               int max_len);

#endif // NCURSES_DIFF_VIEWER_H
