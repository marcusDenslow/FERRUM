/**
 * ncurses_diff_viewer.c
 * NCurses-based interactive diff viewer implementation
 */

#include "ncurses_diff_viewer.h"
#include "git_integration.h"
#include <ctype.h>
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile int terminal_resized = 0;

void handle_sigwinch(int sig) {
  (void)sig;
  terminal_resized = 1;
}

/**
 * Handle terminal resize by recreating all windows
 */
void handle_terminal_resize(NCursesDiffViewer *viewer) {
  if (!viewer)
    return;

  // Clean up old windows
  if (viewer->file_list_win)
    delwin(viewer->file_list_win);
  if (viewer->branch_list_win)
    delwin(viewer->branch_list_win);
  if (viewer->commit_list_win)
    delwin(viewer->commit_list_win);
  if (viewer->stash_list_win)
    delwin(viewer->stash_list_win);
  if (viewer->file_content_win)
    delwin(viewer->file_content_win);
  if (viewer->status_bar_win)
    delwin(viewer->status_bar_win);

  // Clean up search windows during resize
  cleanup_fuzzy_search(viewer);
  cleanup_grep_search(viewer);

  // Reinitialize ncurses with new terminal size
  endwin();
  refresh();
  clear();

  // Get new terminal dimensions
  getmaxyx(stdscr, viewer->terminal_height, viewer->terminal_width);
  viewer->file_panel_width = viewer->terminal_width * 0.4;
  viewer->status_bar_height = viewer->terminal_height * 0.05;
  if (viewer->status_bar_height < 1)
    viewer->status_bar_height = 1;

  int available_height =
      viewer->terminal_height - 1 - viewer->status_bar_height;
  viewer->file_panel_height = available_height * 0.3;
  viewer->commit_panel_height = available_height * 0.3;
  viewer->branch_panel_height = available_height * 0.2;
  viewer->stash_panel_height = available_height - viewer->file_panel_height -
                               viewer->commit_panel_height -
                               viewer->branch_panel_height - 3;

  int status_bar_y = 1 + available_height;

  // Recreate all windows with new dimensions
  viewer->file_list_win =
      newwin(viewer->file_panel_height, viewer->file_panel_width, 1, 0);
  viewer->branch_list_win =
      newwin(viewer->branch_panel_height, viewer->file_panel_width,
             1 + viewer->file_panel_height + 1, 0);
  viewer->commit_list_win = newwin(
      viewer->commit_panel_height, viewer->file_panel_width,
      1 + viewer->file_panel_height + 1 + viewer->branch_panel_height + 1, 0);
  viewer->stash_list_win =
      newwin(viewer->stash_panel_height, viewer->file_panel_width,
             1 + viewer->file_panel_height + 1 + viewer->branch_panel_height +
                 1 + viewer->commit_panel_height + 1,
             0);
  viewer->file_content_win = newwin(
      available_height, viewer->terminal_width - viewer->file_panel_width - 1,
      1, viewer->file_panel_width + 1);
  viewer->status_bar_win = newwin(viewer->status_bar_height,
                                  viewer->terminal_width, status_bar_y, 0);

  // Force complete redraw
  terminal_resized = 0;
}

/**
 * Initialize the ncurses diff viewer
 */
int init_ncurses_diff_viewer(NCursesDiffViewer *viewer) {
  if (!viewer)
    return 0;

  memset(viewer, 0, sizeof(NCursesDiffViewer));
  
  // Initialize fuzzy search
  init_fuzzy_search(viewer);
  
  // Initialize grep search
  init_grep_search(viewer);
  
  viewer->selected_file = 0;
  viewer->file_scroll_offset = 0;
  viewer->file_cursor_line = 0;
  viewer->selected_stash = 0;
  viewer->selected_branch = 0;
  viewer->current_mode = NCURSES_MODE_FILE_LIST;
  viewer->sync_status = SYNC_STATUS_IDLE;
  viewer->spinner_frame = 0;
  viewer->last_sync_time = time(NULL);
  viewer->animation_frame = 0;
  viewer->text_char_count = 0;
  viewer->pushing_branch_index = -1;
  viewer->pulling_branch_index = -1;
  viewer->branch_push_status = SYNC_STATUS_IDLE;
  viewer->branch_pull_status = SYNC_STATUS_IDLE;
  viewer->branch_animation_frame = 0;
  viewer->branch_text_char_count = 0;
  viewer->critical_operation_in_progress = 0;

  viewer->staged_cursor_line = 0;

  viewer->split_view_mode = 0;
  viewer->staged_scroll_offset = 0;
  viewer->active_pane = 0;
  viewer->total_hunks = 0;
  viewer->staged_line_count = 0;
  memset(viewer->current_file_path, 0, sizeof(viewer->current_file_path));

  // Initialize branch commits
  viewer->branch_commit_count = 0;
  viewer->branch_commits_scroll_offset = 0;
  viewer->branch_commits_cursor_line = 0;
  viewer->fetch_pid = -1;
  viewer->fetch_in_progress = 0;
  memset(viewer->current_branch_for_commits, 0,
         sizeof(viewer->current_branch_for_commits));

  // Set locale for Unicode support
  setlocale(LC_ALL, "");

  // Initialize ncurses
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE); // Make getch() non-blocking
  curs_set(0);           // Hide cursor to prevent flickering

  // Enable colors
  if (has_colors()) {
    start_color();
    use_default_colors();           // Enable transparent background
    init_pair(1, COLOR_GREEN, -1);  // Additions
    init_pair(2, COLOR_RED, -1);    // Deletions
    init_pair(3, COLOR_CYAN, -1);   // Headers
    init_pair(4, COLOR_YELLOW, -1); // Selected
    init_pair(5, COLOR_BLACK,
              COLOR_WHITE); // Highlighted selection - keep white background
    init_pair(6, COLOR_MAGENTA, -1); // Orange-ish color for commit hash

    // Gruvbox theme colors for commit info
    init_pair(7, COLOR_CYAN, -1);    // HEAD (blue-green)
    init_pair(8, COLOR_GREEN, -1);   // main branch (nice green)
    init_pair(9, COLOR_RED, -1);     // origin/* (red)
    init_pair(10, COLOR_YELLOW, -1); // commit hash and arrows
  }

  getmaxyx(stdscr, viewer->terminal_height, viewer->terminal_width);
  viewer->file_panel_width =
      viewer->terminal_width * 0.4; // 40% of screen width
  viewer->status_bar_height =
      viewer->terminal_height * 0.05; // 5% of screen height
  if (viewer->status_bar_height < 1)
    viewer->status_bar_height = 1; // Minimum 1 line

  int available_height =
      viewer->terminal_height - 1 -
      viewer->status_bar_height; // Subtract top bar and status bar

  // Distribute height among 4 panels: file, commit, branch, stash
  viewer->file_panel_height = available_height * 0.3;   // 30%
  viewer->commit_panel_height = available_height * 0.3; // 30%
  viewer->branch_panel_height = available_height * 0.2; // 20%
  viewer->stash_panel_height = available_height - viewer->file_panel_height -
                               viewer->commit_panel_height -
                               viewer->branch_panel_height -
                               3; // Rest minus separators

  // Position status bar right after the main content
  int status_bar_y = 1 + available_height;

  // Create six windows: file_list, branch_list, commit_list, stash_list,
  // file_content, status_bar
  viewer->file_list_win =
      newwin(viewer->file_panel_height, viewer->file_panel_width, 1, 0);
  viewer->branch_list_win =
      newwin(viewer->branch_panel_height, viewer->file_panel_width,
             1 + viewer->file_panel_height + 1, 0);
  viewer->commit_list_win = newwin(
      viewer->commit_panel_height, viewer->file_panel_width,
      1 + viewer->file_panel_height + 1 + viewer->branch_panel_height + 1, 0);
  viewer->stash_list_win =
      newwin(viewer->stash_panel_height, viewer->file_panel_width,
             1 + viewer->file_panel_height + 1 + viewer->branch_panel_height +
                 1 + viewer->commit_panel_height + 1,
             0);
  viewer->file_content_win = newwin(
      available_height, viewer->terminal_width - viewer->file_panel_width - 1,
      1, viewer->file_panel_width + 1);
  viewer->status_bar_win = newwin(viewer->status_bar_height,
                                  viewer->terminal_width, status_bar_y, 0);

  if (!viewer->file_list_win || !viewer->file_content_win ||
      !viewer->commit_list_win || !viewer->stash_list_win ||
      !viewer->branch_list_win || !viewer->status_bar_win) {
    cleanup_ncurses_diff_viewer(viewer);
    return 0;
  }

  return 1;
}

/**
 * Get list of changed files from git
 */
int get_ncurses_changed_files(NCursesDiffViewer *viewer) {
  if (!viewer)
    return 0;

  FILE *fp = popen("git status --porcelain 2>/dev/null", "r");
  if (!fp)
    return 0;

  viewer->file_count = 0;
  char line[512];

  while (fgets(line, sizeof(line), fp) != NULL &&
         viewer->file_count < MAX_FILES) {
    char *newline = strchr(line, '\n');
    if (newline)
      *newline = '\0';

    if (strlen(line) < 3)
      continue;

    char staged_status = line[0];
    char unstaged_status = line[1];

    char *filename = line + 3;

    viewer->files[viewer->file_count].status =
        (unstaged_status != ' ' ? unstaged_status : staged_status);
    viewer->files[viewer->file_count].marked_for_commit = 0;
    viewer->files[viewer->file_count].has_staged_changes =
        (staged_status != ' ' && staged_status != '?') ? 1 : 0;

    strncpy(viewer->files[viewer->file_count].filename, filename,
            MAX_FILENAME_LEN - 1);
    viewer->files[viewer->file_count].filename[MAX_FILENAME_LEN - 1] = '\0';

    viewer->file_count++;
  }

  pclose(fp);
  return viewer->file_count;
}

/**
 * Create temporary file with current working version
 */
int create_temp_file_with_changes(const char *filename, char *temp_path) {
  snprintf(temp_path, 256, "/tmp/shell_diff_current_%d", getpid());

  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\" 2>/dev/null", filename,
           temp_path);

  return (system(cmd) == 0);
}

/**
 * Create temporary file with git HEAD version
 */
int create_temp_file_git_version(const char *filename, char *temp_path) {
  snprintf(temp_path, 256, "/tmp/shell_diff_git_%d", getpid());

  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "git show HEAD:\"%s\" > \"%s\" 2>/dev/null",
           filename, temp_path);

  return (system(cmd) == 0);
}

/**
 * Check if a file is a new untracked file
 */
int is_ncurses_new_file(const char *filename) {
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "git ls-files --error-unmatch \"%s\" 2>/dev/null",
           filename);

  FILE *fp = popen(cmd, "r");
  if (!fp)
    return 1; // Assume new if we can't check

  char output[256];
  int is_tracked = (fgets(output, sizeof(output), fp) != NULL);
  pclose(fp);

  return !is_tracked; // Return 1 if not tracked (new file)
}

/**
 * Load file with staging information
 */

int load_file_with_staging_info(NCursesDiffViewer *viewer,
                                const char *filename) {
  if (!viewer || !filename)
    return 0;

  viewer->file_line_count = 0;
  viewer->file_scroll_offset = 0;
  viewer->file_cursor_line = 0;
  viewer->total_hunks = 0;
  viewer->staged_line_count = 0;
  viewer->staged_cursor_line = 0;

  // Store current file path
  strncpy(viewer->current_file_path, filename,
          sizeof(viewer->current_file_path) - 1);
  viewer->current_file_path[sizeof(viewer->current_file_path) - 1] = '\0';

  // Check if this is a new file
  if (is_ncurses_new_file(filename)) {
    // For new files, show first 50 lines as additions
    FILE *fp = fopen(filename, "r");
    if (!fp)
      return 0;

    char line[1024];
    int line_count = 0;
    int current_hunk = 0;

    // Add fake hunk header for new files
    NCursesFileLine *hunk_line = &viewer->file_lines[viewer->file_line_count];
    snprintf(hunk_line->line, sizeof(hunk_line->line), "@@ -0,0 +1,%d @@", 50);
    hunk_line->type = '@';
    hunk_line->is_diff_line = 0;
    hunk_line->hunk_id = current_hunk;
    hunk_line->is_staged = 0;
    hunk_line->line_number_old = 0;
    hunk_line->line_number_new = 1;
    hunk_line->is_context = 0;
    viewer->file_line_count++;

    while (fgets(line, sizeof(line), fp) != NULL &&
           viewer->file_line_count < MAX_FULL_FILE_LINES && line_count < 50) {

      char *newline_pos = strchr(line, '\n');
      if (newline_pos)
        *newline_pos = '\0';

      NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
      snprintf(file_line->line, sizeof(file_line->line), "+%s", line);
      file_line->type = '+';
      file_line->is_diff_line = 1;
      file_line->hunk_id = current_hunk;
      file_line->is_staged = 0;
      file_line->line_number_old = -1;
      file_line->line_number_new = line_count + 1;
      file_line->is_context = 0;

      viewer->file_line_count++;
      line_count++;
    }

    viewer->total_hunks = current_hunk + 1;
    fclose(fp);

    // For new files, also build staged view from what's actually staged
    rebuild_staged_view_from_git(viewer);
    return viewer->file_line_count;
  }

  // Use git diff to get unstaged changes (staging area vs working directory)
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "git diff -U5 \"%s\" 2>/dev/null", filename);

  FILE *diff_fp = popen(cmd, "r");
  if (!diff_fp)
    return 0;

  char diff_line[1024];
  int current_hunk = -1;
  int old_line_num = 0, new_line_num = 0;

  while (fgets(diff_line, sizeof(diff_line), diff_fp) != NULL &&
         viewer->file_line_count < MAX_FULL_FILE_LINES) {

    char *newline_pos = strchr(diff_line, '\n');
    if (newline_pos)
      *newline_pos = '\0';

    // Skip file headers
    if (strncmp(diff_line, "diff --git", 10) == 0 ||
        strncmp(diff_line, "index ", 6) == 0 ||
        strncmp(diff_line, "--- ", 4) == 0 ||
        strncmp(diff_line, "+++ ", 4) == 0) {
      continue;
    }

    NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
    strncpy(file_line->line, diff_line, sizeof(file_line->line) - 1);
    file_line->line[sizeof(file_line->line) - 1] = '\0';
    file_line->is_staged = 0;

    // Process hunk headers and content
    if (diff_line[0] == '@' && diff_line[1] == '@') {
      // Parse hunk header: @@ -old_start,old_count +new_start,new_count @@
      current_hunk++;
      sscanf(diff_line, "@@ -%d,%*d +%d,%*d @@", &old_line_num, &new_line_num);

      file_line->type = '@';
      file_line->is_diff_line = 0;
      file_line->hunk_id = current_hunk;
      file_line->line_number_old = old_line_num;
      file_line->line_number_new = new_line_num;
      file_line->is_context = 0;
    } else if (diff_line[0] == '+') {
      file_line->type = '+';
      file_line->is_diff_line = 1;
      file_line->hunk_id = current_hunk;
      file_line->line_number_old = -1;
      file_line->line_number_new = new_line_num++;
      file_line->is_context = 0;
    } else if (diff_line[0] == '-') {
      file_line->type = '-';
      file_line->is_diff_line = 1;
      file_line->hunk_id = current_hunk;
      file_line->line_number_old = old_line_num++;
      file_line->line_number_new = -1;
      file_line->is_context = 0;
    } else if (diff_line[0] == ' ') {
      file_line->type = ' ';
      file_line->is_diff_line = 0;
      file_line->hunk_id = current_hunk;
      file_line->line_number_old = old_line_num++;
      file_line->line_number_new = new_line_num++;
      file_line->is_context = 1;
    } else {
      continue;
    }

    viewer->file_line_count++;
  }

  viewer->total_hunks = current_hunk + 1;
  pclose(diff_fp);

  // Build staged view from what's actually in git's staging area
  rebuild_staged_view_from_git(viewer);

  return viewer->file_line_count;
}

/**
 * Stage/unstage a single line by line index
 */

// this is a change
// this is another change
int stage_hunk_by_line(NCursesDiffViewer *viewer, int line_index) {
  if (!viewer)
    return 0;

  if (viewer->active_pane == 0) {
    // Unstaged pane - use line_index directly from file_lines
    if (line_index < 0 || line_index >= viewer->file_line_count)
      return 0;

    NCursesFileLine *selected_line = &viewer->file_lines[line_index];

    // Don't stage/unstage hunk headers or context lines
    if (selected_line->type == '@' || selected_line->type == ' ')
      return 0;

    // Only stage actual diff lines (+ or -)
    if (selected_line->type != '+' && selected_line->type != '-')
      return 0;

    // Toggle the staging state
    selected_line->is_staged = !selected_line->is_staged;

  } else {
    // Staged pane - need to find corresponding line in file_lines
    if (line_index < 0 || line_index >= viewer->staged_line_count)
      return 0;

    NCursesFileLine *staged_line = &viewer->staged_lines[line_index];

    // Skip headers and context lines
    if (staged_line->type == '@' || staged_line->type == ' ')
      return 0;

    // Only unstage actual diff lines (+ or -)
    if (staged_line->type != '+' && staged_line->type != '-')
      return 0;

    // Find the corresponding line in file_lines and unstage it
    for (int i = 0; i < viewer->file_line_count; i++) {
      NCursesFileLine *orig_line = &viewer->file_lines[i];

      // Match by content and type
      if (orig_line->type == staged_line->type &&
          strcmp(orig_line->line, staged_line->line) == 0 &&
          orig_line->is_staged) {

        orig_line->is_staged = 0; // Unstage it
        break;
      }
    }
  }

  // Rebuild staged view to reflect changes
  rebuild_staged_view(viewer);
  return 1;
}

/**
 * Rebuild staged view with proper git patch format for individual lines
 */
void rebuild_staged_view(NCursesDiffViewer *viewer) {
  if (!viewer)
    return;

  viewer->staged_line_count = 0;

  // Check if we have any staged changes
  int has_staged_changes = 0;
  for (int i = 0; i < viewer->file_line_count; i++) {
    if (viewer->file_lines[i].is_staged) {
      has_staged_changes = 1;
      break;
    }
  }

  if (!has_staged_changes) {
    return;
  }

  // Add git diff header
  NCursesFileLine *header = &viewer->staged_lines[viewer->staged_line_count++];
  snprintf(header->line, sizeof(header->line), "diff --git a/%s b/%s",
           viewer->current_file_path, viewer->current_file_path);
  header->type = '@';
  header->is_diff_line = 0;
  header->is_staged = 1;
  header->is_context = 0;

  // Add index line
  header = &viewer->staged_lines[viewer->staged_line_count++];
  snprintf(header->line, sizeof(header->line), "index 13bdd0a..9abd450 100644");
  header->type = '@';
  header->is_diff_line = 0;
  header->is_staged = 1;
  header->is_context = 0;

  // Add file headers
  header = &viewer->staged_lines[viewer->staged_line_count++];
  snprintf(header->line, sizeof(header->line), "--- a/%s",
           viewer->current_file_path);
  header->type = '@';
  header->is_diff_line = 0;
  header->is_staged = 1;
  header->is_context = 0;

  header = &viewer->staged_lines[viewer->staged_line_count++];
  snprintf(header->line, sizeof(header->line), "+++ b/%s",
           viewer->current_file_path);
  header->type = '@';
  header->is_diff_line = 0;
  header->is_staged = 1;
  header->is_context = 0;

  // Process each hunk that has staged changes
  for (int hunk = 0; hunk < viewer->total_hunks; hunk++) {
    // Check if this hunk has staged changes
    int hunk_has_staged = 0;
    for (int i = 0; i < viewer->file_line_count; i++) {
      if (viewer->file_lines[i].hunk_id == hunk &&
          viewer->file_lines[i].is_staged) {
        hunk_has_staged = 1;
        break;
      }
    }

    if (!hunk_has_staged)
      continue;

    // Find the hunk boundaries
    int hunk_start = -1, hunk_end = -1;
    for (int i = 0; i < viewer->file_line_count; i++) {
      if (viewer->file_lines[i].hunk_id == hunk) {
        if (hunk_start == -1)
          hunk_start = i;
        hunk_end = i;
      }
    }

    if (hunk_start == -1)
      continue;

    // Calculate new hunk header with adjusted line counts
    int old_start = -1, new_start = -1;
    int old_count = 0, new_count = 0;

    // Find first line numbers
    for (int i = hunk_start; i <= hunk_end; i++) {
      NCursesFileLine *line = &viewer->file_lines[i];
      if (line->type == '@') {
        old_start = line->line_number_old;
        new_start = line->line_number_new;
      } else if (line->is_staged || line->is_context) {
        if (line->type != '+')
          old_count++;
        if (line->type != '-')
          new_count++;
      }
    }

    // Add hunk header
    NCursesFileLine *staged_header =
        &viewer->staged_lines[viewer->staged_line_count++];
    snprintf(staged_header->line, sizeof(staged_header->line),
             "@@ -%d,%d +%d,%d @@", old_start, old_count, new_start, new_count);
    staged_header->type = '@';
    staged_header->is_diff_line = 0;
    staged_header->is_staged = 1;
    staged_header->is_context = 0;

    // Add context lines before staged changes
    for (int i = hunk_start; i <= hunk_end; i++) {
      NCursesFileLine *line = &viewer->file_lines[i];
      if (line->type == '@')
        continue;

      // Always include context lines and staged diff lines
      if (line->is_context || line->is_staged) {
        NCursesFileLine *staged_line =
            &viewer->staged_lines[viewer->staged_line_count++];
        *staged_line = *line;
        staged_line->is_staged = 1;

        if (viewer->staged_line_count >= MAX_FULL_FILE_LINES)
          break;
      }
    }

    if (viewer->staged_line_count >= MAX_FULL_FILE_LINES)
      break;
  }
}

/**
 * Rebuild staged view from what's actually staged in git
 */
void rebuild_staged_view_from_git(NCursesDiffViewer *viewer) {
  if (!viewer)
    return;

  viewer->staged_line_count = 0;

  // Get staged changes from git (HEAD vs staging area)
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "git diff --cached -U5 \"%s\" 2>/dev/null",
           viewer->current_file_path);

  FILE *diff_fp = popen(cmd, "r");
  if (!diff_fp)
    return;

  char diff_line[1024];
  int has_any_staged = 0;

  // First pass: check if there are any staged changes
  while (fgets(diff_line, sizeof(diff_line), diff_fp) != NULL) {
    char *newline_pos = strchr(diff_line, '\n');
    if (newline_pos)
      *newline_pos = '\0';

    // Skip file headers
    if (strncmp(diff_line, "diff --git", 10) == 0 ||
        strncmp(diff_line, "index ", 6) == 0 ||
        strncmp(diff_line, "--- ", 4) == 0 ||
        strncmp(diff_line, "+++ ", 4) == 0) {
      continue;
    }

    if (strlen(diff_line) > 0) {
      has_any_staged = 1;
      break;
    }
  }
  pclose(diff_fp);

  if (!has_any_staged) {
    return;
  }

  // Second pass: actually build the staged view
  diff_fp = popen(cmd, "r");
  if (!diff_fp)
    return;

  while (fgets(diff_line, sizeof(diff_line), diff_fp) != NULL &&
         viewer->staged_line_count < MAX_FULL_FILE_LINES) {

    char *newline_pos = strchr(diff_line, '\n');
    if (newline_pos)
      *newline_pos = '\0';

    // Skip file headers for the first few lines, but include them for patch
    // format
    if (strncmp(diff_line, "diff --git", 10) == 0 ||
        strncmp(diff_line, "index ", 6) == 0 ||
        strncmp(diff_line, "--- ", 4) == 0 ||
        strncmp(diff_line, "+++ ", 4) == 0) {

      NCursesFileLine *staged_line =
          &viewer->staged_lines[viewer->staged_line_count];
      strncpy(staged_line->line, diff_line, sizeof(staged_line->line) - 1);
      staged_line->line[sizeof(staged_line->line) - 1] = '\0';
      staged_line->type = '@';
      staged_line->is_diff_line = 0;
      staged_line->is_staged = 1;
      staged_line->is_context = 0;
      viewer->staged_line_count++;
      continue;
    }

    NCursesFileLine *staged_line =
        &viewer->staged_lines[viewer->staged_line_count];
    strncpy(staged_line->line, diff_line, sizeof(staged_line->line) - 1);
    staged_line->line[sizeof(staged_line->line) - 1] = '\0';
    staged_line->is_staged = 1;

    // Set line type for proper coloring
    if (diff_line[0] == '@' && diff_line[1] == '@') {
      staged_line->type = '@';
      staged_line->is_diff_line = 0;
      staged_line->is_context = 0;
    } else if (diff_line[0] == '+') {
      staged_line->type = '+';
      staged_line->is_diff_line = 1;
      staged_line->is_context = 0;
    } else if (diff_line[0] == '-') {
      staged_line->type = '-';
      staged_line->is_diff_line = 1;
      staged_line->is_context = 0;
    } else if (diff_line[0] == ' ') {
      staged_line->type = ' ';
      staged_line->is_diff_line = 0;
      staged_line->is_context = 1;
    } else {
      staged_line->type = ' ';
      staged_line->is_diff_line = 0;
      staged_line->is_context = 0;
    }

    viewer->staged_line_count++;
  }

  pclose(diff_fp);
}

/**
 * Apply staged changes using git
 */

int apply_staged_changes(NCursesDiffViewer *viewer) {
  if (!viewer || viewer->staged_line_count == 0)
    return 0;

  char *patch_content = malloc(50000);
  if (!patch_content)
    return 0;

  strcpy(patch_content, "");
  for (int i = 0; i < viewer->staged_line_count; i++) {
    strcat(patch_content, viewer->staged_lines[i].line);
    strcat(patch_content, "\n");
  }

  char patch_filename[256];
  snprintf(patch_filename, sizeof(patch_filename), "/tmp/lazygit-%d-%ld.patch",
           getpid(), time(NULL));

  FILE *patch_file = fopen(patch_filename, "w");
  if (!patch_file) {
    free(patch_content);
    return 0;
  }

  fprintf(patch_file, "%s", patch_content);
  fclose(patch_file);
  free(patch_content);

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "git apply --cached \"%s\" >/dev/null 2>&1",
           patch_filename);
  int result = system(cmd);

  unlink(patch_filename);

  if (result == 0) {
    // Don't clear staging state - reload from git instead
    get_ncurses_changed_files(viewer);
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_file_with_staging_info(
          viewer, viewer->files[viewer->selected_file].filename);
    }
  }
  return (result == 0);
}

/**
 * Unstage a specific line from the staged pane
 */

int unstage_line_from_git(NCursesDiffViewer *viewer, int staged_line_index) {
  if (!viewer || staged_line_index < 0 ||
      staged_line_index >= viewer->staged_line_count)
    return 0;

  NCursesFileLine *line = &viewer->staged_lines[staged_line_index];

  // Skip headers and context lines
  if (line->type == '@' || line->type == ' ')
    return 0;

  // Only unstage actual diff lines (+ or -)
  if (line->type != '+' && line->type != '-')
    return 0;

  // For now, let's just reset the entire file and let user re-stage what they
  // want This is simpler and more reliable than trying to create reverse
  // patches
  char reset_cmd[512];
  snprintf(reset_cmd, sizeof(reset_cmd),
           "git reset HEAD \"%s\" >/dev/null 2>&1", viewer->current_file_path);
  int result = system(reset_cmd);

  if (result == 0) {
    // Reload after unstaging
    get_ncurses_changed_files(viewer);
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_file_with_staging_info(
          viewer, viewer->files[viewer->selected_file].filename);
    }
    return 1;
  }

  return 0;
}

/**
 * Reset staged changes
 */

int reset_staged_changes(NCursesDiffViewer *viewer) {
  if (!viewer)
    return 0;

  for (int i = 0; i < viewer->file_line_count; i++) {
    viewer->file_lines[i].is_staged = 0;
  }

  viewer->staged_line_count = 0;
  rebuild_staged_view(viewer);

  return 1;
}

/**
 * Draw a box with rounded corners
 */
void draw_rounded_box(WINDOW *win) {
  if (!win)
    return;

  int height, width;
  getmaxyx(win, height, width);

  // Draw horizontal lines
  for (int x = 1; x < width - 1; x++) {
    mvwaddch(win, 0, x, ACS_HLINE);
    mvwaddch(win, height - 1, x, ACS_HLINE);
  }

  // Draw vertical lines
  for (int y = 1; y < height - 1; y++) {
    mvwaddch(win, y, 0, ACS_VLINE);
    mvwaddch(win, y, width - 1, ACS_VLINE);
  }

  // Draw rounded corners
  mvwaddch(win, 0, 0, ACS_ULCORNER);
  mvwaddch(win, 0, width - 1, ACS_URCORNER);
  mvwaddch(win, height - 1, 0, ACS_LLCORNER);
  mvwaddch(win, height - 1, width - 1, ACS_LRCORNER);
}

/**
 * Get commit history
 */
int get_commit_history(NCursesDiffViewer *viewer) {
  if (!viewer)
    return 0;

  FILE *fp =
      popen("git log --oneline -20 --format=\"%h|%an|%s\" 2>/dev/null", "r");
  if (!fp)
    return 0;

  viewer->commit_count = 0;
  char line[512];

  // Get list of unpushed commits first
  char unpushed_hashes[MAX_COMMITS][16];
  int unpushed_count = 0;

  FILE *unpushed_fp =
      popen("git log origin/HEAD..HEAD --format=\"%h\" 2>/dev/null", "r");
  if (unpushed_fp) {
    while (fgets(line, sizeof(line), unpushed_fp) != NULL &&
           unpushed_count < MAX_COMMITS) {
      char *newline = strchr(line, '\n');
      if (newline)
        *newline = '\0';
      strncpy(unpushed_hashes[unpushed_count], line,
              sizeof(unpushed_hashes[unpushed_count]) - 1);
      unpushed_hashes[unpushed_count]
                     [sizeof(unpushed_hashes[unpushed_count]) - 1] = '\0';
      unpushed_count++;
    }
    pclose(unpushed_fp);
  }

  // If origin/HEAD doesn't exist, try origin/main and origin/master
  if (unpushed_count == 0) {
    unpushed_fp =
        popen("git log origin/main..HEAD --format=\"%h\" 2>/dev/null", "r");
    if (unpushed_fp) {
      while (fgets(line, sizeof(line), unpushed_fp) != NULL &&
             unpushed_count < MAX_COMMITS) {
        char *newline = strchr(line, '\n');
        if (newline)
          *newline = '\0';
        strncpy(unpushed_hashes[unpushed_count], line,
                sizeof(unpushed_hashes[unpushed_count]) - 1);
        unpushed_hashes[unpushed_count]
                       [sizeof(unpushed_hashes[unpushed_count]) - 1] = '\0';
        unpushed_count++;
      }
      pclose(unpushed_fp);
    }
  }

  if (unpushed_count == 0) {
    unpushed_fp =
        popen("git log origin/master..HEAD --format=\"%h\" 2>/dev/null", "r");
    if (unpushed_fp) {
      while (fgets(line, sizeof(line), unpushed_fp) != NULL &&
             unpushed_count < MAX_COMMITS) {
        char *newline = strchr(line, '\n');
        if (newline)
          *newline = '\0';
        strncpy(unpushed_hashes[unpushed_count], line,
                sizeof(unpushed_hashes[unpushed_count]) - 1);
        unpushed_hashes[unpushed_count]
                       [sizeof(unpushed_hashes[unpushed_count]) - 1] = '\0';
        unpushed_count++;
      }
      pclose(unpushed_fp);
    }
  }

  while (fgets(line, sizeof(line), fp) != NULL &&
         viewer->commit_count < MAX_COMMITS) {
    // Remove newline
    char *newline = strchr(line, '\n');
    if (newline)
      *newline = '\0';

    // Parse format: hash|author|title
    char *hash = strtok(line, "|");
    char *author = strtok(NULL, "|");
    char *title = strtok(NULL, "|");

    if (hash && author && title) {
      // Store commit hash (first 7 chars)
      strncpy(viewer->commits[viewer->commit_count].hash, hash,
              sizeof(viewer->commits[viewer->commit_count].hash) - 1);
      viewer->commits[viewer->commit_count]
          .hash[sizeof(viewer->commits[viewer->commit_count].hash) - 1] = '\0';

      // Store first two letters of author name
      viewer->commits[viewer->commit_count].author_initials[0] =
          author[0] ? author[0] : '?';
      viewer->commits[viewer->commit_count].author_initials[1] =
          author[1] ? author[1] : '?';
      viewer->commits[viewer->commit_count].author_initials[2] = '\0';

      // Store title
      strncpy(viewer->commits[viewer->commit_count].title, title,
              MAX_COMMIT_TITLE_LEN - 1);
      viewer->commits[viewer->commit_count].title[MAX_COMMIT_TITLE_LEN - 1] =
          '\0';

      // Check if this commit is in the unpushed list
      viewer->commits[viewer->commit_count].is_pushed = 1; // Default to pushed
      for (int i = 0; i < unpushed_count; i++) {
        if (strcmp(viewer->commits[viewer->commit_count].hash,
                   unpushed_hashes[i]) == 0) {
          viewer->commits[viewer->commit_count].is_pushed = 0;
          break;
        }
      }

      viewer->commit_count++;
    }
  }

  pclose(fp);
  return viewer->commit_count;
}

/**
 * Toggle file marking for commit
 */
void toggle_file_mark(NCursesDiffViewer *viewer, int file_index) {
  if (!viewer || file_index < 0 || file_index >= viewer->file_count)
    return;

  viewer->files[file_index].marked_for_commit =
      !viewer->files[file_index].marked_for_commit;
}

/**
 * Mark all files for commit (or unmark if all are already marked)
 */
void mark_all_files(NCursesDiffViewer *viewer) {
  if (!viewer)
    return;

  // Check if all files are already marked
  int all_marked = 1;
  for (int i = 0; i < viewer->file_count; i++) {
    if (!viewer->files[i].marked_for_commit) {
      all_marked = 0;
      break;
    }
  }

  // If all are marked, unmark all. Otherwise, mark all
  for (int i = 0; i < viewer->file_count; i++) {
    viewer->files[i].marked_for_commit = all_marked ? 0 : 1;
  }
}

/**
 * Show confirmation dialog for diverged branch
 * Returns 1 for force push, 0 for cancel
 */
int show_diverged_branch_dialog(int commits_ahead, int commits_behind) {
  // Save current screen
  WINDOW *saved_screen = dupwin(stdscr);

  // Calculate window dimensions
  int dialog_width = 60;
  int dialog_height = 8;
  int start_x = COLS / 2 - dialog_width / 2;
  int start_y = LINES / 2 - dialog_height / 2;

  // Create dialog window
  WINDOW *dialog_win = newwin(dialog_height, dialog_width, start_y, start_x);
  if (!dialog_win) {
    if (saved_screen)
      delwin(saved_screen);
    return 0;
  }

  // Draw dialog
  wattron(dialog_win, COLOR_PAIR(3)); // Red for warning
  box(dialog_win, 0, 0);

  // Title
  mvwprintw(dialog_win, 1, 2, "Branch has diverged!");

  // Message
  mvwprintw(dialog_win, 3, 2, "Local: %d commit(s) ahead", commits_ahead);
  mvwprintw(dialog_win, 4, 2, "Remote: %d commit(s) ahead", commits_behind);

  // Instructions
  mvwprintw(dialog_win, 6, 2, "Force push anyway? (y/N):");
  wattroff(dialog_win, COLOR_PAIR(3));

  wrefresh(dialog_win);

  // Get user input
  int ch;
  int result = 0;

  while ((ch = wgetch(dialog_win)) != ERR) {
    if (ch == 'y' || ch == 'Y') {
      result = 1;
      break;
    } else if (ch == 'n' || ch == 'N' || ch == 27 ||
               ch == 'q') { // ESC or q or n
      result = 0;
      break;
    } else if (ch == '\n' || ch == '\r') {
      result = 0; // Default to no on Enter
      break;
    }
  }

  // Cleanup
  delwin(dialog_win);
  if (saved_screen) {
    touchwin(saved_screen);
    wrefresh(saved_screen);
    delwin(saved_screen);
  }

  return result;
}

/**
 * Show confirmation dialog requiring user to type "yes"
 * Returns 1 if confirmed, 0 if cancelled
 */
int show_reset_confirmation_dialog(void) {
  // Save current screen
  WINDOW *saved_screen = dupwin(stdscr);

  // Calculate window dimensions
  int dialog_width = 60;
  int dialog_height = 10;
  int start_x = COLS / 2 - dialog_width / 2;
  int start_y = LINES / 2 - dialog_height / 2;

  // Create dialog window
  WINDOW *dialog_win = newwin(dialog_height, dialog_width, start_y, start_x);
  if (!dialog_win) {
    if (saved_screen)
      delwin(saved_screen);
    return 0;
  }

  char input_buffer[10] = "";
  int input_pos = 0;

  while (1) {
    // Clear and redraw dialog
    werase(dialog_win);
    wattron(dialog_win, COLOR_PAIR(3)); // Red for warning
    box(dialog_win, 0, 0);

    // Title
    mvwprintw(dialog_win, 1, 2, "HARD RESET WARNING!");

    // Warning message
    mvwprintw(dialog_win, 3, 2, "This will permanently delete the most recent");
    mvwprintw(dialog_win, 4, 2, "commit and ALL uncommitted changes!");

    // Instructions
    mvwprintw(dialog_win, 6, 2, "Type 'yes' to confirm or ESC to cancel:");

    // Input field
    mvwprintw(dialog_win, 7, 2, "> %s", input_buffer);

    wattroff(dialog_win, COLOR_PAIR(3));
    wrefresh(dialog_win);

    // Position cursor
    wmove(dialog_win, 7, 4 + strlen(input_buffer));

    // Get user input
    int ch = wgetch(dialog_win);

    if (ch == 27 || ch == 'q') { // ESC or q
      break;
    } else if (ch == '\n' || ch == '\r') {
      // Check if input is "yes" (case insensitive)
      if (strcasecmp(input_buffer, "yes") == 0) {
        // Cleanup and return confirmed
        delwin(dialog_win);
        if (saved_screen) {
          touchwin(saved_screen);
          wrefresh(saved_screen);
          delwin(saved_screen);
        }
        return 1;
      } else {
        // Wrong input, clear buffer
        input_buffer[0] = '\0';
        input_pos = 0;
      }
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      // Backspace
      if (input_pos > 0) {
        input_pos--;
        input_buffer[input_pos] = '\0';
      }
    } else if (ch >= 32 && ch <= 126 && input_pos < 8) {
      // Regular character
      input_buffer[input_pos] = ch;
      input_pos++;
      input_buffer[input_pos] = '\0';
    }
  }

  // Cleanup and return cancelled
  delwin(dialog_win);
  if (saved_screen) {
    touchwin(saved_screen);
    wrefresh(saved_screen);
    delwin(saved_screen);
  }

  return 0;
}

/**
 * Get commit title and message input from user
 */

int get_commit_title_input(char *title, int max_len, char *message,
                           int max_message_len) {
  if (!title)
    return 0;

  // Calculate window dimensions
  int input_width = COLS * 0.8; // 80% of screen width
  int title_height = 3;
  int message_height = 15; // Much taller message box
  int start_x = COLS / 2 - input_width / 2;
  int title_start_y = LINES / 2 - (title_height + message_height + 2) / 2;
  int message_start_y = title_start_y + title_height + 1;

  // Create title and message windows directly (no outer dialog)
  WINDOW *title_win = newwin(title_height, input_width, title_start_y, start_x);
  WINDOW *message_win =
      newwin(message_height, input_width, message_start_y, start_x);

  if (!title_win || !message_win) {
    if (title_win)
      delwin(title_win);
    if (message_win)
      delwin(message_win);
    return 0;
  }

  // Initialize message buffer
  if (message) {
    message[0] = '\0';
  }

  // Variables for input handling
  char local_message[2048] = "";
  int current_field = 0;         // 0 = title, 1 = message
  int title_scroll_offset = 0;   // For horizontal scrolling
  int message_scroll_offset = 0; // For vertical scrolling
  int ch;

  // Function to redraw title window
  void redraw_title() {
    werase(title_win);
    box(title_win, 0, 0);

    int visible_width = input_width - 4;
    int title_len = strlen(title);

    // CRITICAL: Clear the content area with spaces FIRST
    for (int x = 1; x <= visible_width; x++) {
      mvwaddch(title_win, 1, x, ' ');
    }

    // Calculate what part of the title to show
    int display_start = title_scroll_offset;
    int display_end = display_start + visible_width;
    if (display_end > title_len)
      display_end = title_len;

    // Show the visible portion of the title ON TOP of cleared spaces
    for (int i = display_start; i < display_end; i++) {
      mvwaddch(title_win, 1, 1 + (i - display_start), title[i]);
    }

    // Highlight header if active
    if (current_field == 0) {
      wattron(title_win, COLOR_PAIR(4));
      mvwprintw(title_win, 0, 2, " Title (Tab to switch, Enter to commit) ");
      wattroff(title_win, COLOR_PAIR(4));
    } else {
      mvwprintw(title_win, 0, 2, " Title (Tab to switch, Enter to commit) ");
    }
    wrefresh(title_win);
  }

  // Function to redraw message window - SIMPLE like title field
  void redraw_message() {
    werase(message_win);
    box(message_win, 0, 0);

    int visible_height = message_height - 2;
    int message_visible_width = input_width - 3;

    // Clear content area
    for (int y = 1; y <= visible_height; y++) {
      for (int x = 1; x <= message_visible_width; x++) {
        mvwaddch(message_win, y, x, ' ');
      }
    }

    // SIMPLE: Just display the raw string, character by character
    int y = 1, x = 1;
    for (int i = 0; local_message[i] && y <= visible_height; i++) {
      if (local_message[i] == '\n') {
        y++;
        x = 1;
      } else {
        if (x <= message_visible_width) {
          mvwaddch(message_win, y, x, local_message[i]);
          x++;
          if (x > message_visible_width) {
            y++;
            x = 1;
          }
        }
      }
    }

    // Highlight header if active
    if (current_field == 1) {
      wattron(message_win, COLOR_PAIR(4));
      mvwprintw(message_win, 0, 2,
                " Message (Tab to switch, Enter for newline) ");
      wattroff(message_win, COLOR_PAIR(4));
    } else {
      mvwprintw(message_win, 0, 2,
                " Message (Tab to switch, Enter for newline) ");
    }
    wrefresh(message_win);
  }

  // Initial draw
  redraw_title();
  redraw_message();

  // Position initial cursor
  if (current_field == 0) {
    int cursor_pos = strlen(title) - title_scroll_offset;
    int visible_width = input_width - 4;
    if (cursor_pos > visible_width - 1)
      cursor_pos = visible_width - 1;
    if (cursor_pos < 0)
      cursor_pos = 0;
    wmove(title_win, 1, 1 + cursor_pos);
    wrefresh(title_win);
  }

  curs_set(1);
  noecho();

  // Main input loop
  while (1) {
    ch = getch();
    int redraw_needed = 0;

    if (ch == 27) {
      // ESC to cancel - don't commit
      if (message && max_message_len > 0) {
        message[0] = '\0'; // Clear message
      }
      title[0] = '\0'; // Clear title
      break;
    }

    if (ch == '\t') {
      // Switch between fields
      current_field = !current_field;
      redraw_needed = 1;
    } else if (ch == '\n' || ch == '\r') {
      if (current_field == 0) {
        // Enter in title field - commit if title has content
        if (strlen(title) > 0) {
          break;
        }
      } else {
        // Enter in message field - add newline
        int len = strlen(local_message);
        if (len < 2047) {
          local_message[len] = '\n';
          local_message[len + 1] = '\0';

          // Auto-scroll down if needed
          int visible_height = message_height - 2;
          int current_lines = 1;
          for (int i = 0; local_message[i]; i++) {
            if (local_message[i] == '\n')
              current_lines++;
          }
          if (current_lines > visible_height + message_scroll_offset) {
            message_scroll_offset += 2;
          }
          redraw_message();
        }
      }
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      // Handle backspace
      if (current_field == 0) {
        int len = strlen(title);
        if (len > 0) {
          title[len - 1] = '\0';

          // Adjust scroll if needed
          int visible_width = input_width - 4;
          if (len - 1 <= title_scroll_offset) {
            title_scroll_offset = (len - 1) - (visible_width - 5);
            if (title_scroll_offset < 0)
              title_scroll_offset = 0;
          }
          redraw_title();
        }
      } else {
        int len = strlen(local_message);
        if (len > 0) {
          local_message[len - 1] = '\0';
          redraw_message();
        }
      }
    } else if (ch >= 32 && ch <= 126) {
      // Regular character input
      if (current_field == 0) {
        int len = strlen(title);
        if (len < max_len - 1) {
          title[len] = ch;
          title[len + 1] = '\0';

          // Auto-scroll horizontally if needed
          int visible_width = input_width - 4;
          if (len + 1 > title_scroll_offset + visible_width - 5) {
            title_scroll_offset = (len + 1) - (visible_width - 5);
          }
          redraw_title();
        }
      } else {
        int len = strlen(local_message);
        if (len < 2047) {
          local_message[len] = ch;
          local_message[len + 1] = '\0';
          redraw_message();
        }
      }
    }

    // Redraw if field switched
    if (redraw_needed) {
      redraw_title();
      redraw_message();
    }

    // Position cursor in active field
    if (current_field == 0) {
      int cursor_pos = strlen(title) - title_scroll_offset;
      int visible_width = input_width - 4;
      if (cursor_pos > visible_width - 1)
        cursor_pos = visible_width - 1;
      if (cursor_pos < 0)
        cursor_pos = 0;
      wmove(title_win, 1, 1 + cursor_pos);
      wrefresh(title_win);
    } else {
      // Position cursor in message field - EXACTLY like title field
      int message_len = strlen(local_message);
      int message_visible_width = input_width - 3;

      // Simple cursor positioning - just count where we are
      int y = 1, x = 1;
      for (int i = 0; i < message_len; i++) {
        if (local_message[i] == '\n') {
          y++;
          x = 1;
        } else {
          x++;
          if (x > message_visible_width) {
            y++;
            x = 1;
          }
        }
      }

      // Position cursor at end of text
      wmove(message_win, y, x);
      wrefresh(message_win);
    }
  }

  // Copy local message to output parameter only if not canceled
  if (message && max_message_len > 0 && strlen(title) > 0) {
    strncpy(message, local_message, max_message_len - 1);
    message[max_message_len - 1] = '\0';
  }

  // Restore settings
  curs_set(0); // Hide cursor

  for (int y = title_start_y; y < title_start_y + title_height; y++) {
    move(y, start_x);
    for (int x = 0; x < input_width; x++) {
      addch(' ');
    }
  }

  // Clear message window area
  for (int y = message_start_y; y < message_start_y + message_height; y++) {
    move(y, start_x);
    for (int x = 0; x < input_width; x++) {
      addch(' ');
    }
  }

  // Clean up windows
  delwin(title_win);
  delwin(message_win);

  return strlen(title) > 0 ? 1 : 0;
}

/**
 * Commit marked files with title and message
 */
int commit_marked_files(NCursesDiffViewer *viewer, const char *commit_title,
                        const char *commit_message) {
  if (!viewer || !commit_title || strlen(commit_title) == 0)
    return 0;

  // First, add marked files to git
  for (int i = 0; i < viewer->file_count; i++) {
    if (viewer->files[i].marked_for_commit) {
      char cmd[1024];
      snprintf(cmd, sizeof(cmd), "git add \"%s\" 2>/dev/null >/dev/null",
               viewer->files[i].filename);
      system(cmd);
    }
  }

  // Write commit message to temp file
  char temp_msg_file[256];
  snprintf(temp_msg_file, sizeof(temp_msg_file), "/tmp/commit_msg_%d",
           getpid());

  FILE *msg_file = fopen(temp_msg_file, "w");
  if (!msg_file)
    return 0;

  if (commit_message && strlen(commit_message) > 0) {
    fprintf(msg_file, "%s\n\n%s", commit_title, commit_message);
  } else {
    fprintf(msg_file, "%s", commit_title);
  }
  fclose(msg_file);

  char commit_cmd[512];
  snprintf(commit_cmd, sizeof(commit_cmd),
           "git commit -F \"%s\" 2>/dev/null >/dev/null", temp_msg_file);
  int result = system(commit_cmd);
  unlink(temp_msg_file);

  if (result == 0) {
    // Small delay to ensure git has processed the commit
    usleep(100000); // 100ms delay

    // Refresh file list and commit history
    get_ncurses_changed_files(viewer);
    get_commit_history(viewer);
    get_ncurses_git_branches(viewer);

    // Reset selection if no files remain
    if (viewer->file_count == 0) {
      viewer->selected_file = 0;
      viewer->file_line_count = 0;
      viewer->file_scroll_offset = 0;
    } else if (viewer->selected_file >= viewer->file_count) {
      viewer->selected_file = viewer->file_count - 1;
    }

    // Reload the currently selected file's diff if files still exist
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_file_with_staging_info(
          viewer, viewer->files[viewer->selected_file].filename);
    }

    werase(viewer->branch_list_win);
    render_branch_list_window(viewer);
    wrefresh(viewer->branch_list_win);

    return 1;
  }

  return 0;
}

/**
 * Reset commit (soft) - undo commit but keep changes
 */
int reset_commit_soft(NCursesDiffViewer *viewer, int commit_index) {
  if (!viewer || commit_index < 0 || commit_index >= viewer->commit_count)
    return 0;

  // Only allow reset of the most recent commit (index 0)
  if (commit_index != 0)
    return 0;

  // Do soft reset of HEAD~1
  int result = system("git reset --soft HEAD~1 2>/dev/null >/dev/null");

  if (result == 0) {
    // Small delay to ensure git has processed the reset
    usleep(100000); // 100ms delay

    // Refresh everything
    get_ncurses_changed_files(viewer);
    get_commit_history(viewer);

    // Reload current file if any files exist
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_file_with_staging_info(
          viewer, viewer->files[viewer->selected_file].filename);
    }

    return 1;
  }

  return 0;
}

/**
 * Reset commit (hard) - undo commit and discard changes
 */
int reset_commit_hard(NCursesDiffViewer *viewer, int commit_index) {
  if (!viewer || commit_index < 0 || commit_index >= viewer->commit_count)
    return 0;

  // Only allow reset of the most recent commit (index 0)
  if (commit_index != 0)
    return 0;

  // Show confirmation dialog
  if (!show_reset_confirmation_dialog()) {
    return 0; // User cancelled
  }

  // Do hard reset of HEAD~1
  int result = system("git reset --hard HEAD~1 2>/dev/null >/dev/null");

  if (result == 0) {
    // Small delay to ensure git has processed the reset
    usleep(100000); // 100ms delay

    // Refresh everything
    get_ncurses_changed_files(viewer);
    get_commit_history(viewer);

    // Reset file selection since changes are discarded
    viewer->selected_file = 0;
    viewer->file_line_count = 0;
    viewer->file_scroll_offset = 0;

    // Reload current file if any files exist
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_file_with_staging_info(
          viewer, viewer->files[viewer->selected_file].filename);
    }

    return 1;
  }

  return 0;
}

/**
 * Amend the most recent commit
 */
int amend_commit(NCursesDiffViewer *viewer) {
  if (!viewer || viewer->commit_count == 0)
    return 0;

  // Get current commit message
  char current_title[MAX_COMMIT_TITLE_LEN] = "";
  char current_message[2048] = "";

  // Get the current commit message
  FILE *fp = popen("git log -1 --pretty=format:%s 2>/dev/null", "r");
  if (fp) {
    fgets(current_title, sizeof(current_title), fp);
    pclose(fp);
  }

  // Get the current commit body (if any)
  fp = popen("git log -1 --pretty=format:%b 2>/dev/null", "r");
  if (fp) {
    fgets(current_message, sizeof(current_message), fp);
    pclose(fp);
  }

  // Get new commit message from user (pre-filled with current message)
  char new_title[MAX_COMMIT_TITLE_LEN];
  char new_message[2048];

  strncpy(new_title, current_title, sizeof(new_title) - 1);
  new_title[sizeof(new_title) - 1] = '\0';
  strncpy(new_message, current_message, sizeof(new_message) - 1);
  new_message[sizeof(new_message) - 1] = '\0';

  if (get_commit_title_input(new_title, MAX_COMMIT_TITLE_LEN, new_message,
                             sizeof(new_message))) {
    // Add any marked files first
    for (int i = 0; i < viewer->file_count; i++) {
      if (viewer->files[i].marked_for_commit) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "git add \"%s\" 2>/dev/null >/dev/null",
                 viewer->files[i].filename);
        system(cmd);
      }
    }

    // Write amended commit message to temp file
    char temp_msg_file[256];
    snprintf(temp_msg_file, sizeof(temp_msg_file), "/tmp/amend_msg_%d",
             getpid());

    FILE *msg_file = fopen(temp_msg_file, "w");
    if (!msg_file)
      return 0;

    if (strlen(new_message) > 0) {
      fprintf(msg_file, "%s\n\n%s", new_title, new_message);
    } else {
      fprintf(msg_file, "%s", new_title);
    }
    fclose(msg_file);

    char amend_cmd[512];
    snprintf(amend_cmd, sizeof(amend_cmd),
             "git commit --amend -F \"%s\" 2>/dev/null >/dev/null",
             temp_msg_file);
    int result = system(amend_cmd);
    unlink(temp_msg_file);

    // this is a change i want
    // this is a change i dont want

    if (result == 0) {
      // Small delay to ensure git has processed the amend
      usleep(100000); // 100ms delay

      // Refresh everything after ammending
      get_ncurses_changed_files(viewer);
      get_commit_history(viewer);
      get_ncurses_git_branches(viewer);

      // Reset selection if no files remain
      if (viewer->file_count == 0) {
        viewer->selected_file = 0;
        viewer->file_line_count = 0;
        viewer->file_scroll_offset = 0;
      } else if (viewer->selected_file >= viewer->file_count) {
        viewer->selected_file = viewer->file_count - 1;
      }

      // Reload current file if any files exist
      if (viewer->file_count > 0 &&
          viewer->selected_file < viewer->file_count) {
        load_file_with_staging_info(
            viewer, viewer->files[viewer->selected_file].filename);
      }

      return 1;
    }
  }

  return 0;
}

int get_github_credentials(char *username, int username_len, char *token,
                           int token_len) {
  if (!username || !token)
    return 0;

  int dialog_width = 60;
  int dialog_height = 10;
  int start_x = COLS / 2 - dialog_width / 2;
  int start_y = LINES / 2 - dialog_height / 2;

  WINDOW *cred_win = newwin(dialog_height, dialog_width, start_y, start_x);
  if (!cred_win)
    return 0;

  char temp_username[256] = "";
  char temp_token[256] = "";
  int current_field = 0;
  int username_pos = 0;
  int token_pos = 0;

  curs_set(1);

  while (1) {
    werase(cred_win);
    box(cred_win, 0, 0);

    mvwprintw(cred_win, 1, 2, "Github Authentication Required");

    if (current_field == 0) {
      wattron(cred_win, COLOR_PAIR(4));
      mvwprintw(cred_win, 3, 2, "Username: %s", temp_username);
      wattroff(cred_win, COLOR_PAIR(4));
    } else {
      mvwprintw(cred_win, 3, 2, "Username: %s", temp_username);
    }

    if (current_field == 1) {
      wattron(cred_win, COLOR_PAIR(4));
      mvwprintw(cred_win, 4, 2, "Token:    ");

      for (int i = 0; i < token_pos; i++) {
        waddch(cred_win, '*');
      }
      wattroff(cred_win, COLOR_PAIR(4));
    } else {
      mvwprintw(cred_win, 4, 2, "Token:    ");
      for (int i = 0; i < token_pos; i++) {
        waddch(cred_win, '*');
      }
    }

    mvwprintw(cred_win, 6, 2, "Get token at: github.com/settings/tokens");
    mvwprintw(cred_win, 7, 2,
              "Tab: switch fields | Enter: confirm | Esc: cancel");

    if (current_field == 0) {
      wmove(cred_win, 3, 12 + username_pos);
    } else {
      wmove(cred_win, 4, 12 + token_pos);
    }

    wrefresh(cred_win);

    int ch = getch();

    switch (ch) {
    case 27:
      delwin(cred_win);
      curs_set(0);
      return 0;

    case '\t':
      current_field = (current_field + 1) % 2;
      break;

    case '\n':
    case '\r':
      if (strlen(temp_username) > 0 && strlen(temp_token) > 0) {
        strncpy(username, temp_username, username_len - 1);
        username[username_len - 1] = '\0';
        strncpy(token, temp_token, token_len - 1);
        token[token_len - 1] = '\0';
        delwin(cred_win);
        curs_set(0);
        return 1;
      }
      break;

    case KEY_BACKSPACE:
    case 127:
    case 8:
      if (current_field == 0 && username_pos > 0) {
        username_pos--;
        temp_username[username_pos] = '\0';
      } else if (current_field == 1 && token_pos > 0) {
        token_pos--;
        temp_token[token_pos] = '\0';
      }
      break;

    default:
      if (ch >= 32 && ch <= 126) {
        if (current_field == 0 && username_pos < 256) {
          temp_username[username_pos] = ch;
          username_pos++;
          temp_username[username_pos] = '\0';
        } else if (current_field == 1 && token_pos < 256) {
          temp_token[token_pos] = ch;
          token_pos++;
          temp_token[token_pos] = '\0';
        }
      }
      break;
    }
  }
}

int execute_git_with_auth(const char *base_cmd, const char *username,
                          const char *token) {
  if (!base_cmd || !username || !token)
    return 1;

  char remote_url[1024] = "";
  FILE *fp = popen("git config --get remote.origin.url 2>/dev/null", "r");
  if (!fp)
    return 1;

  if (fgets(remote_url, sizeof(remote_url), fp) == NULL) {
    pclose(fp);
    return 1;
  }
  pclose(fp);

  char *newline = strchr(remote_url, '\n');
  if (newline)
    *newline = '\0';

  char auth_url[2048];
  if (strstr(remote_url, "https://github.com/")) {
    char *repo_part = remote_url + strlen("https://github.com/");
    snprintf(auth_url, sizeof(auth_url), "https://%s:%s@github.com/%s",
             username, token, repo_part);
  } else if (strstr(remote_url, "git@github.com:")) {
    char *repo_part = strchr(remote_url, ':') + 1;
    if (strstr(repo_part, ".git")) {
      *(strstr(repo_part, ".git")) = '\0';
    }
    snprintf(auth_url, sizeof(auth_url), "https://%s%s@github.com%s", username,
             token, repo_part);
  } else {
    return 1;
  }

  char auth_cmd[4096];
  snprintf(auth_cmd, sizeof(auth_cmd), "%s %s >/dev/null >&1", base_cmd,
           auth_url);

  return system(auth_cmd);
}

/**
 * Push specific commit
 */
int push_commit(NCursesDiffViewer *viewer, int commit_index) {
  if (!viewer || commit_index < 0 || commit_index >= viewer->commit_count)
    return 0;

  // Check if current branch has upstream
  char current_branch[256];
  if (!get_current_branch_name(current_branch, sizeof(current_branch))) {
    show_error_popup("Failed to get current branch name");
    viewer->sync_status = SYNC_STATUS_IDLE;
    return 0;
  }

  if (!branch_has_upstream(current_branch)) {
    // Show upstream selection dialog
    char upstream_selection[512];
    if (show_upstream_selection_dialog(current_branch, upstream_selection,
                                       sizeof(upstream_selection))) {
      // User selected an upstream, set it up
      char cmd[1024];
      snprintf(cmd, sizeof(cmd), "git push --set-upstream %s >/dev/null 2>&1",
               upstream_selection);

      int result = system(cmd);
      if (result == 0) {
        // Upstream set successfully, show success and refresh
        viewer->sync_status = SYNC_STATUS_PUSHED_APPEARING;
        viewer->animation_frame = 0;
        viewer->text_char_count = 0;
        get_commit_history(viewer);

        // Only refresh the commit pane
        werase(viewer->commit_list_win);
        render_commit_list_window(viewer);
        wrefresh(viewer->commit_list_win);

        return 1;
      } else {
        show_error_popup(
            "Failed to set upstream and push. Check your connection.");
      }
    }

    viewer->sync_status = SYNC_STATUS_IDLE;
    return 0;
  }

  // Check for branch divergence first
  int commits_ahead = 0;
  int commits_behind = 0;
  int is_diverged = check_branch_divergence(&commits_ahead, &commits_behind);

  // If diverged, show confirmation dialog
  if (is_diverged) {
    if (!show_diverged_branch_dialog(commits_ahead, commits_behind)) {
      // User cancelled
      viewer->sync_status = SYNC_STATUS_IDLE;
      return 0;
    }
  }

  // Animation already started by key handler for immediate feedback

  for (int i = 0; i < viewer->branch_count; i++) {
    if (viewer->branches[i].status == 1) { // Current branch
      viewer->pushing_branch_index = i;
      break;
    }
  }

  // Set branch-specific push status
  viewer->branch_push_status = SYNC_STATUS_PUSHING_VISIBLE;
  viewer->branch_animation_frame = 0;
  viewer->branch_text_char_count = 7; // Show full "Pushing" immediately

  // Force immediate branch window refresh to show "Pushing" before the blocking
  // git operation

  werase(viewer->branch_list_win);
  render_file_list_window(viewer);
  render_file_content_window(viewer);
  render_commit_list_window(viewer);
  render_branch_list_window(viewer);
  render_stash_list_window(viewer);
  render_status_bar(viewer);

  // Create a simple animated push with spinner updates
  pid_t push_pid;
  int result = 0;

  // Try push first without authentication
  char push_cmd[256];
  if (is_diverged) {
    strcpy(push_cmd, "git push --force-with-lease origin");
  } else {
    strcpy(push_cmd, "git push origin");
  }

  push_pid = fork();
  if (push_pid == 0) {
    // Child process: try normal push first
    exit(system(is_diverged
                    ? "git push --force-with-lease origin >/dev/null 2>&1"
                    : "git push origin >/dev/null 2>&1"));
  }

  // Parent process: wait and check if authentication is needed
  if (push_pid > 0) {
    int status;
    int spinner_counter = 0;

    while (waitpid(push_pid, &status, WNOHANG) == 0) {
      // Update spinner animation
      viewer->branch_animation_frame = spinner_counter;
      spinner_counter = (spinner_counter + 1) % 40;

      render_file_list_window(viewer);
      render_file_content_window(viewer);
      render_commit_list_window(viewer);
      render_branch_list_window(viewer);
      render_stash_list_window(viewer);
      render_status_bar(viewer);

      usleep(100000); // 100ms delay
    }

    // Check if push failed (likely due to authentication)
    if (WIFEXITED(status)) {
      result = WEXITSTATUS(status);
    } else {
      result = 1;
    }

    // If push failed, try with authentication
    if (result != 0) {
      char username[256] = "";
      char token[256] = "";

      // Clear screen and get credentials
      clear();
      refresh();

      if (get_github_credentials(username, sizeof(username), token,
                                 sizeof(token))) {
        // Try authenticated push
        result = execute_git_with_auth(push_cmd, username, token);

        // Clear credentials from memory for security
        memset(username, 0, sizeof(username));
        memset(token, 0, sizeof(token));
      } else {
        result = 1; // User cancelled
      }

      // Force complete screen refresh after credential dialog
      clear();
      refresh();
    }
  } else {
    result = 1; // Fork failed
  }

  // Parent process: animate the spinner while push is happening
  if (push_pid > 0) {
    int status;
    int spinner_counter = 0;

    while (waitpid(push_pid, &status, WNOHANG) == 0) {
      // Update spinner animation
      viewer->branch_animation_frame = spinner_counter;
      spinner_counter = (spinner_counter + 1) % 40;

      // Refresh ALL windows, not just branch window
      render_file_list_window(viewer);
      render_file_content_window(viewer);
      render_commit_list_window(viewer);
      render_branch_list_window(viewer);
      render_stash_list_window(viewer);
      render_status_bar(viewer);

      // Small delay for animation timing
      usleep(100000); // 100ms delay
    }

    // Get the exit status of the push command
    if (WIFEXITED(status)) {
      result = WEXITSTATUS(status);
    } else {
      result = 1; // Error
    }
  } else {
    result = 1; // Fork failed
  }

  if (result == 0) {
    // Immediately transition to "Pushed!" animation
    viewer->sync_status = SYNC_STATUS_PUSHED_APPEARING;
    viewer->animation_frame = 0;
    viewer->text_char_count = 0;

    // Set branch-specific pushed status
    viewer->branch_push_status = SYNC_STATUS_PUSHED_APPEARING;
    viewer->branch_animation_frame = 0;
    viewer->branch_text_char_count = 0;

    // Refresh commit history to get proper push status
    get_commit_history(viewer);
    get_ncurses_git_branches(viewer); // Add this line to refresh branch status

    // Refresh both commit and branch panes
    werase(viewer->commit_list_win);
    render_commit_list_window(viewer);
    wrefresh(viewer->commit_list_win);

    werase(viewer->branch_list_win);
    render_branch_list_window(viewer);
    wrefresh(viewer->branch_list_win);

    return 1;
  } else {
    // Push failed, show error
    show_error_popup("Push failed. Check your network, credentials, or get a "
                     "Personal Access Token from github.com/settings/tokens");
    viewer->sync_status = SYNC_STATUS_IDLE;
    viewer->pushing_branch_index = -1;
    viewer->branch_push_status = SYNC_STATUS_IDLE;
  }

  return 0;
}

/**
 * Pull commits from remote
 */
int pull_commits(NCursesDiffViewer *viewer) {
  if (!viewer)
    return 0;

  // Start pulling animation immediately
  viewer->sync_status = SYNC_STATUS_PULLING_APPEARING;
  viewer->animation_frame = 0;
  viewer->text_char_count = 0;

  // Render immediately to show the animation start
  render_status_bar(viewer);

  // Do the actual pull work
  int result = system("git pull origin 2>/dev/null >/dev/null");

  if (result == 0) {
    // Refresh everything after pull
    get_ncurses_changed_files(viewer);
    get_commit_history(viewer);

    // Reset selection if no files remain
    if (viewer->file_count == 0) {
      viewer->selected_file = 0;
      viewer->file_line_count = 0;
      viewer->file_scroll_offset = 0;
    } else if (viewer->selected_file >= viewer->file_count) {
      viewer->selected_file = viewer->file_count - 1;
    }

    // Reload current file if any
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_file_with_staging_info(
          viewer, viewer->files[viewer->selected_file].filename);
    }

    return 1;
  }

  return 0;
}

/**
 * Render the file list window
 */
void render_file_list_window(NCursesDiffViewer *viewer) {
  if (!viewer || !viewer->file_list_win)
    return;

  // CRITICAL: Clear the entire window first
  werase(viewer->file_list_win);

  // Draw rounded border and title
  draw_rounded_box(viewer->file_list_win);
  mvwprintw(viewer->file_list_win, 0, 2, " 1. Files ");

  int max_files_visible = viewer->file_panel_height - 2;

  // CRITICAL: Clear the content area with spaces FIRST
  for (int y = 1; y < viewer->file_panel_height - 1; y++) {
    for (int x = 1; x < viewer->file_panel_width - 1; x++) {
      mvwaddch(viewer->file_list_win, y, x, ' ');
    }
  }

  for (int i = 0; i < max_files_visible; i++) {
    int y = i + 1;

    // Skip if no more files
    if (i >= viewer->file_count)
      continue;

    // Check if this line should be highlighted
    int is_selected = (i == viewer->selected_file &&
                       viewer->current_mode == NCURSES_MODE_FILE_LIST);
    int is_marked = (i == viewer->selected_file &&
                     viewer->current_mode != NCURSES_MODE_FILE_LIST);

    // Apply line highlight if selected
    if (is_selected) {
      wattron(viewer->file_list_win, COLOR_PAIR(5));
    }

    // Show selection indicator
    if (is_selected) {
      mvwprintw(viewer->file_list_win, y, 1, ">");
    } else if (is_marked) {
      wattron(viewer->file_list_win, COLOR_PAIR(1));
      mvwprintw(viewer->file_list_win, y, 1, "*");
      wattroff(viewer->file_list_win, COLOR_PAIR(1));
    } else {
      mvwprintw(viewer->file_list_win, y, 1, " ");
    }

    // Status indicator - turn off highlight temporarily for colored status
    if (is_selected) {
      wattroff(viewer->file_list_win, COLOR_PAIR(5));
    }

    char status = viewer->files[i].status;
    if (status == 'M') {
      wattron(viewer->file_list_win, COLOR_PAIR(4));
      mvwprintw(viewer->file_list_win, y, 2, "M");
      wattroff(viewer->file_list_win, COLOR_PAIR(4));
    } else if (status == 'A') {
      wattron(viewer->file_list_win, COLOR_PAIR(1));
      mvwprintw(viewer->file_list_win, y, 2, "A");
      wattroff(viewer->file_list_win, COLOR_PAIR(1));
    } else if (status == 'D') {
      wattron(viewer->file_list_win, COLOR_PAIR(2));
      mvwprintw(viewer->file_list_win, y, 2, "D");
      wattroff(viewer->file_list_win, COLOR_PAIR(2));
    } else {
      mvwprintw(viewer->file_list_win, y, 2, "%c", status);
    }

    // Restore highlight if it was on
    if (is_selected) {
      wattron(viewer->file_list_win, COLOR_PAIR(5));
    }

    // Filename (truncated to fit panel with "..")
    int max_name_len = viewer->file_panel_width - 6; // Leave space for border
    char truncated_name[256];
    if ((int)strlen(viewer->files[i].filename) > max_name_len) {
      strncpy(truncated_name, viewer->files[i].filename, max_name_len - 2);
      truncated_name[max_name_len - 2] = '\0';
      strcat(truncated_name, "..");
    } else {
      strcpy(truncated_name, viewer->files[i].filename);
    }

    // show staged indicator and filename

    if (viewer->files[i].has_staged_changes) {
      if (is_selected) {
        wattroff(viewer->file_list_win, COLOR_PAIR(5));
      }

      wattron(viewer->file_list_win, COLOR_PAIR(1)); // green for staged
      mvwprintw(viewer->file_list_win, y, 4, "5");
      wattroff(viewer->file_list_win, COLOR_PAIR(1));
      if (is_selected) {
        wattron(viewer->file_list_win, COLOR_PAIR(5));
      }

      if (viewer->files[i].marked_for_commit) {
        if (is_selected) {
          wattroff(viewer->file_list_win, COLOR_PAIR(5));
        }

        wattron(viewer->file_list_win, COLOR_PAIR(1));
        mvwprintw(viewer->file_list_win, y, 5, " %s", truncated_name);
        wattroff(viewer->file_list_win, COLOR_PAIR(1));
        if (is_selected) {
          wattron(viewer->file_list_win, COLOR_PAIR(5));
        }
      } else {
        mvwprintw(viewer->file_list_win, y, 5, " %s", truncated_name);
      }
    } else {
      if (viewer->files[i].marked_for_commit) {
        if (is_selected) {
          wattroff(viewer->file_list_win, COLOR_PAIR(5));
        }
        wattron(viewer->file_list_win, COLOR_PAIR(1));
        mvwprintw(viewer->file_list_win, y, 4, " %s", truncated_name);
        wattroff(viewer->file_list_win, COLOR_PAIR(1));
        if (is_selected) {
          wattron(viewer->file_list_win, COLOR_PAIR(5));
        }
      } else {
        mvwprintw(viewer->file_list_win, y, 4, "%s", truncated_name);
      }
    }

    // Turn off line highlight if it was applied
    if (is_selected) {
      wattroff(viewer->file_list_win, COLOR_PAIR(5));
    }
  }

  wrefresh(viewer->file_list_win);
}

/**
 * Render the commit list window
 */

void render_commit_list_window(NCursesDiffViewer *viewer) {
  if (!viewer || !viewer->commit_list_win)
    return;

  // CRITICAL: Clear the entire window first
  werase(viewer->commit_list_win);

  // Draw rounded border and title
  draw_rounded_box(viewer->commit_list_win);
  mvwprintw(viewer->commit_list_win, 0, 2, " 4. Commits ");

  int max_commits_visible = viewer->commit_panel_height - 2;

  // CRITICAL: Clear the content area with spaces FIRST
  for (int y = 1; y < viewer->commit_panel_height - 1; y++) {
    for (int x = 1; x < viewer->file_panel_width - 1; x++) {
      mvwaddch(viewer->commit_list_win, y, x, ' ');
    }
  }

  for (int i = 0; i < max_commits_visible; i++) {
    int y = i + 1;

    // Skip if no more commits
    if (i >= viewer->commit_count)
      continue;

    // Check if this commit line should be highlighted
    int is_selected_commit = (i == viewer->selected_commit &&
                              viewer->current_mode == NCURSES_MODE_COMMIT_LIST);
    
    // Check if this commit is currently being viewed
    int is_being_viewed = (i == viewer->selected_commit &&
                           viewer->current_mode == NCURSES_MODE_COMMIT_VIEW);

    // Apply line highlight if selected
    if (is_selected_commit) {
      wattron(viewer->commit_list_win, COLOR_PAIR(5));
    }

    // Show view indicator (currently being viewed)
    if (is_being_viewed) {
      wattron(viewer->commit_list_win, COLOR_PAIR(1)); // Green for active view
      mvwprintw(viewer->commit_list_win, y, 0, "*");
      wattroff(viewer->commit_list_win, COLOR_PAIR(1));
    } else {
      mvwprintw(viewer->commit_list_win, y, 0, " ");
    }

    // Show selection indicator
    if (is_selected_commit) {
      mvwprintw(viewer->commit_list_win, y, 1, ">");
    } else {
      mvwprintw(viewer->commit_list_win, y, 1, " ");
    }

    // Show commit hash with color based on push status
    if (is_selected_commit) {
      wattroff(viewer->commit_list_win, COLOR_PAIR(5));
    }

    // Color commit hash based on push status: yellow for pushed, red for
    // unpushed
    if (viewer->commits[i].is_pushed) {
      wattron(viewer->commit_list_win,
              COLOR_PAIR(4)); // Yellow for pushed commits
    } else {
      wattron(viewer->commit_list_win,
              COLOR_PAIR(2)); // Red for unpushed commits
    }
    mvwprintw(viewer->commit_list_win, y, 2, "%s", viewer->commits[i].hash);
    if (viewer->commits[i].is_pushed) {
      wattroff(viewer->commit_list_win, COLOR_PAIR(4));
    } else {
      wattroff(viewer->commit_list_win, COLOR_PAIR(2));
    }

    // Show author initials - use cyan for a subtle contrast
    wattron(viewer->commit_list_win, COLOR_PAIR(3)); // Cyan for author initials
    mvwprintw(viewer->commit_list_win, y, 10, "%s",
              viewer->commits[i].author_initials);
    wattroff(viewer->commit_list_win, COLOR_PAIR(3));

    if (is_selected_commit) {
      wattron(viewer->commit_list_win, COLOR_PAIR(5));
    }

    // Show commit title (always white, truncated to fit with "..")
    int max_title_len = viewer->file_panel_width - 15; // Leave space for border
    char truncated_title[256];
    if ((int)strlen(viewer->commits[i].title) > max_title_len) {
      strncpy(truncated_title, viewer->commits[i].title, max_title_len - 2);
      truncated_title[max_title_len - 2] = '\0';
      strcat(truncated_title, "..");
    } else {
      strcpy(truncated_title, viewer->commits[i].title);
    }

    mvwprintw(viewer->commit_list_win, y, 13, "%s", truncated_title);

    // Turn off selection highlighting if this was the selected commit
    if (is_selected_commit) {
      wattroff(viewer->commit_list_win, COLOR_PAIR(5));
    }
  }

  wrefresh(viewer->commit_list_win);
}

/**
 * Render the file content window with split staging view
 */
void render_file_content_window(NCursesDiffViewer *viewer) {
  if (!viewer || !viewer->file_content_win)
    return;

  int height, width;
  getmaxyx(viewer->file_content_win, height, width);

  // Clear the window
  werase(viewer->file_content_win);
  draw_rounded_box(viewer->file_content_win);

  if (!viewer->split_view_mode) {
    // Show preview content in list modes and view modes
    if (viewer->current_mode == NCURSES_MODE_FILE_LIST ||
        viewer->current_mode == NCURSES_MODE_COMMIT_LIST ||
        viewer->current_mode == NCURSES_MODE_COMMIT_VIEW ||
        viewer->current_mode == NCURSES_MODE_BRANCH_LIST ||
        viewer->current_mode == NCURSES_MODE_BRANCH_VIEW ||
        viewer->current_mode == NCURSES_MODE_STASH_LIST ||
        viewer->current_mode == NCURSES_MODE_STASH_VIEW) {
      
      // Add title based on current mode
      const char* title = "";
      switch (viewer->current_mode) {
        case NCURSES_MODE_FILE_LIST:
          title = " File Diff Preview ";
          break;
        case NCURSES_MODE_COMMIT_LIST:
          title = " Commit Details ";
          break;
        case NCURSES_MODE_COMMIT_VIEW:
          title = " Commit Diff ";
          break;
        case NCURSES_MODE_BRANCH_LIST:
          title = " Branch Commits ";
          break;
        case NCURSES_MODE_BRANCH_VIEW:
          title = " Branch Details ";
          break;
        case NCURSES_MODE_STASH_LIST:
          title = " Stash Details ";
          break;
        case NCURSES_MODE_STASH_VIEW:
          title = " Stash Diff ";
          break;
        default:
          title = " Preview ";
          break;
      }
      mvwprintw(viewer->file_content_win, 0, 2, "%s", title);
      
      // Render preview content using the loaded file_lines
      if (viewer->file_line_count > 0) {
        int max_lines_visible = height - 2;
        int display_count = 0;
        
        for (int i = viewer->file_scroll_offset;
             i < viewer->file_line_count && display_count < max_lines_visible;
             i++) {
          
          NCursesFileLine *line = &viewer->file_lines[i];
          int is_cursor_line = (i == viewer->file_cursor_line);
          
          // Calculate how many display lines this logical line will need
          int line_height = calculate_wrapped_line_height(line->line, width - 4);
          
          // Skip if this line would exceed remaining space
          if (display_count + line_height > max_lines_visible) {
            break;
          }
          
          int y = display_count + 1;
          int color_pair = 0;
          
          // Determine color based on line type
          if (line->type == '@') {
            color_pair = 3; // Cyan for hunk headers
          } else if (line->type == '+') {
            color_pair = 1; // Green for additions
          } else if (line->type == '-') {
            color_pair = 2; // Red for deletions
          }
          
          // Render the line with wrapping
          int rows_used = render_wrapped_line(viewer->file_content_win, line->line, 
                                             y, 1, width, line_height, color_pair, is_cursor_line);
          display_count += rows_used;
        }
      } else {
        // No content to show
        mvwprintw(viewer->file_content_win, height/2, (width - 15)/2, "No preview available");
      }
    }
    
    wrefresh(viewer->file_content_win);
    return;
  }

  // Split view mode for file staging
  int split_line = height / 2;
  int unstaged_height = split_line - 1;
  int staged_height = height - split_line - 2;

  // Draw split line
  for (int x = 1; x < width - 1; x++) {
    mvwaddch(viewer->file_content_win, split_line, x, ACS_HLINE);
  }

  // Render unstaged changes pane
  if (viewer->active_pane == 0) {
    wattron(viewer->file_content_win, COLOR_PAIR(4));
  }
  mvwprintw(viewer->file_content_win, 0, 2, " Unstaged changes ");
  if (viewer->active_pane == 0) {
    wattroff(viewer->file_content_win, COLOR_PAIR(4));
  }

  // Show unstaged lines with wrapping
  int unstaged_display_count = 0;
  for (int i = viewer->file_scroll_offset;
       i < viewer->file_line_count &&
       unstaged_display_count < unstaged_height - 1;
       i++) {

    NCursesFileLine *line = &viewer->file_lines[i];
    int is_cursor_line = (i == viewer->file_cursor_line && viewer->active_pane == 0);
    
    // Calculate how many display lines this logical line will need
    int line_height = calculate_wrapped_line_height(line->line, width - 4);
    
    // Skip if this line would exceed remaining space
    if (unstaged_display_count + line_height > unstaged_height - 1) {
      break;
    }

    int y = unstaged_display_count + 1;
    int color_pair = 0;
    
    // Determine color based on line type
    if (line->type == '@') {
      color_pair = 3; // Cyan for hunk headers
    } else if (line->is_staged) {
      color_pair = 3; // Dimmed for staged content
    } else if (line->type == '+') {
      color_pair = 1; // Green for additions
    } else if (line->type == '-') {
      color_pair = 2; // Red for deletions
    }

    // Handle staged indicator for diff lines
    if (line->is_staged && (line->type == '+' || line->type == '-')) {
      // First render the staged indicator
      if (is_cursor_line) {
        wattron(viewer->file_content_win, A_REVERSE);
      }
      wattron(viewer->file_content_win, COLOR_PAIR(1));
      mvwaddch(viewer->file_content_win, y, 1, '*');
      wattroff(viewer->file_content_win, COLOR_PAIR(1));
      if (is_cursor_line) {
        wattroff(viewer->file_content_win, A_REVERSE);
      }
      
      // Then render the line content starting from column 2, skipping first char
      char line_without_prefix[1024];
      strcpy(line_without_prefix, line->line + 1);
      int rows_used = render_wrapped_line(viewer->file_content_win, line_without_prefix, 
                                         y, 2, width, line_height, color_pair, is_cursor_line);
      unstaged_display_count += rows_used;
    } else {
      // Regular line rendering
      int rows_used = render_wrapped_line(viewer->file_content_win, line->line, 
                                         y, 1, width, line_height, color_pair, is_cursor_line);
      unstaged_display_count += rows_used;
    }
  }

  // Render staged changes pane
  if (viewer->active_pane == 1) {
    wattron(viewer->file_content_win, COLOR_PAIR(1));
  }
  mvwprintw(viewer->file_content_win, split_line, 2, " Staged changes ");
  if (viewer->active_pane == 1) {
    wattroff(viewer->file_content_win, COLOR_PAIR(1));
  }

  // Show staged lines with proper git patch format and wrapping
  int staged_display_count = 0;
  for (int i = viewer->staged_scroll_offset;
       i < viewer->staged_line_count &&
       staged_display_count < staged_height - 1;
       i++) {

    NCursesFileLine *line = &viewer->staged_lines[i];
    int is_cursor_line = (i == viewer->staged_cursor_line && viewer->active_pane == 1);
    
    // Calculate how many display lines this logical line will need
    int line_height = calculate_wrapped_line_height(line->line, width - 4);
    
    // Skip if this line would exceed remaining space
    if (staged_display_count + line_height > staged_height - 1) {
      break;
    }

    int y = split_line + 1 + staged_display_count;
    int color_pair = 0;
    
    // Determine color based on line type
    if (line->type == '+') {
      color_pair = 1; // Green for additions
    } else if (line->type == '-') {
      color_pair = 2; // Red for deletions
    } else if (line->type == '@') {
      color_pair = 3; // Cyan for headers
    }

    // Render the line with wrapping
    int rows_used = render_wrapped_line(viewer->file_content_win, line->line, 
                                       y, 1, width, line_height, color_pair, is_cursor_line);
    staged_display_count += rows_used;
  }

  wrefresh(viewer->file_content_win);
}

/**
 * Render the status bar
 */
void render_status_bar(NCursesDiffViewer *viewer) {
  if (!viewer || !viewer->status_bar_win)
    return;

  // Clear status bar (no border)
  werase(viewer->status_bar_win);
  wbkgd(viewer->status_bar_win, COLOR_PAIR(3)); // Cyan background

  // Left side: Key bindings based on current mode
  char keybindings[256] = "";
  if (viewer->current_mode == NCURSES_MODE_FILE_LIST) {
    strcpy(keybindings, "Stage: <space> | Stage All: a | Stash: s | Commit: c");
  } else if (viewer->current_mode == NCURSES_MODE_COMMIT_LIST) {
    strcpy(keybindings, "Push: P | Pull: p | Reset: r/R | Amend: a | Nav: j/k");
  } else if (viewer->current_mode == NCURSES_MODE_STASH_LIST) {
    strcpy(keybindings, "Apply: <space> | Pop: g | Drop: d | Nav: j/k");
  } else if (viewer->current_mode == NCURSES_MODE_BRANCH_LIST) {
    strcpy(keybindings, "View: Enter | Checkout: c | New: n | Rename: r | "
                        "Delete: d | Pull: p | Nav: j/k");
  } else if (viewer->current_mode == NCURSES_MODE_FILE_VIEW) {
    strcpy(keybindings, "Scroll: j/k | Page: Ctrl+U/D | Back: Esc");
  } else if (viewer->current_mode == NCURSES_MODE_COMMIT_VIEW) {
    strcpy(keybindings, "Scroll: j/k | Page: Ctrl+U/D | Back: Esc");
  } else if (viewer->current_mode == NCURSES_MODE_STASH_VIEW) {
    strcpy(keybindings, "Scroll: j/k | Page: Ctrl+U/D | Back: Esc");
  } else if (viewer->current_mode == NCURSES_MODE_BRANCH_VIEW) {
    strcpy(keybindings, "Scroll: j/k | Page: Ctrl+U/D | Back: Esc");
  }

  mvwprintw(viewer->status_bar_win, 0, 1, "%s", keybindings);

  // Right side: Sync status
  char sync_text[64] = "";
  char *spinner_chars[] = {"|", "/", "-", "\\"};
  int spinner_idx = (viewer->spinner_frame / 2) %
                    4; // Change every frame (~20ms per character)

  if (viewer->sync_status == SYNC_STATUS_IDLE) {
    // Show nothing when idle
    strcpy(sync_text, "");
  } else if (viewer->sync_status >= SYNC_STATUS_SYNCING_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_SYNCING_DISAPPEARING) {
    // Show partial or full "Fetching" text with spinner
    char full_text[] = "Fetching";
    int chars_to_show = viewer->text_char_count;
    if (chars_to_show > 8)
      chars_to_show = 8; // Max length of "Fetching"
    if (chars_to_show < 0)
      chars_to_show = 0;

    if (chars_to_show > 0) {
      char partial_text[16];
      strncpy(partial_text, full_text, chars_to_show);
      partial_text[chars_to_show] = '\0';

      if (viewer->sync_status == SYNC_STATUS_SYNCING_VISIBLE) {
        snprintf(sync_text, sizeof(sync_text), "%s %s", partial_text,
                 spinner_chars[spinner_idx]);
      } else {
        strcpy(sync_text, partial_text);
      }
    } else {
      strcpy(sync_text, "");
    }
  } else if (viewer->sync_status >= SYNC_STATUS_PUSHING_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_PUSHING_DISAPPEARING) {
    // Show partial or full "Pushing" text with spinner
    char full_text[] = "Pushing";
    int chars_to_show = viewer->text_char_count;
    if (chars_to_show > 7)
      chars_to_show = 7; // Max length of "Pushing"
    if (chars_to_show < 0)
      chars_to_show = 0;

    if (chars_to_show > 0) {
      char partial_text[16];
      strncpy(partial_text, full_text, chars_to_show);
      partial_text[chars_to_show] = '\0';

      if (viewer->sync_status == SYNC_STATUS_PUSHING_VISIBLE) {
        snprintf(sync_text, sizeof(sync_text), "%s %s", partial_text,
                 spinner_chars[spinner_idx]);
      } else {
        strcpy(sync_text, partial_text);
      }
    } else {
      strcpy(sync_text, "");
    }
  } else if (viewer->sync_status >= SYNC_STATUS_PULLING_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_PULLING_DISAPPEARING) {
    // Show partial or full "Pulling" text with spinner
    char full_text[] = "Pulling";
    int chars_to_show = viewer->text_char_count;
    if (chars_to_show > 7)
      chars_to_show = 7; // Max length of "Pulling"
    if (chars_to_show < 0)
      chars_to_show = 0;

    if (chars_to_show > 0) {
      char partial_text[16];
      strncpy(partial_text, full_text, chars_to_show);
      partial_text[chars_to_show] = '\0';

      if (viewer->sync_status == SYNC_STATUS_PULLING_VISIBLE) {
        snprintf(sync_text, sizeof(sync_text), "%s %s", partial_text,
                 spinner_chars[spinner_idx]);
      } else {
        strcpy(sync_text, partial_text);
      }
    } else {
      strcpy(sync_text, "");
    }
  } else if (viewer->sync_status >= SYNC_STATUS_SYNCED_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_SYNCED_DISAPPEARING) {
    // Show partial or full "Synced!" text (for fetching)
    char full_text[] = "Synced!";
    int chars_to_show = viewer->text_char_count;
    if (chars_to_show > 7)
      chars_to_show = 7; // Max length of "Synced!"
    if (chars_to_show < 0)
      chars_to_show = 0;

    strncpy(sync_text, full_text, chars_to_show);
    sync_text[chars_to_show] = '\0';
  } else if (viewer->sync_status >= SYNC_STATUS_PUSHED_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_PUSHED_DISAPPEARING) {
    // Show partial or full "Pushed!" text
    char full_text[] = "Pushed!";
    int chars_to_show = viewer->text_char_count;
    if (chars_to_show > 7)
      chars_to_show = 7; // Max length of "Pushed!"
    if (chars_to_show < 0)
      chars_to_show = 0;

    strncpy(sync_text, full_text, chars_to_show);
    sync_text[chars_to_show] = '\0';
  } else if (viewer->sync_status >= SYNC_STATUS_PULLED_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_PULLED_DISAPPEARING) {
    // Show partial or full "Pulled!" text
    char full_text[] = "Pulled!";
    int chars_to_show = viewer->text_char_count;
    if (chars_to_show > 7)
      chars_to_show = 7; // Max length of "Pulled!"
    if (chars_to_show < 0)
      chars_to_show = 0;

    strncpy(sync_text, full_text, chars_to_show);
    sync_text[chars_to_show] = '\0';
  }

  if (strlen(sync_text) > 0) {
    int sync_text_pos = viewer->terminal_width - strlen(sync_text) -
                        1; // No border padding needed

    if (viewer->sync_status >= SYNC_STATUS_SYNCED_APPEARING &&
            viewer->sync_status <= SYNC_STATUS_SYNCED_DISAPPEARING ||
        viewer->sync_status >= SYNC_STATUS_PUSHED_APPEARING &&
            viewer->sync_status <= SYNC_STATUS_PUSHED_DISAPPEARING ||
        viewer->sync_status >= SYNC_STATUS_PULLED_APPEARING &&
            viewer->sync_status <= SYNC_STATUS_PULLED_DISAPPEARING) {
      wattron(viewer->status_bar_win,
              COLOR_PAIR(1)); // Green for success messages
      mvwprintw(viewer->status_bar_win, 0, sync_text_pos, "%s", sync_text);
      wattroff(viewer->status_bar_win, COLOR_PAIR(1));
    } else {
      wattron(viewer->status_bar_win,
              COLOR_PAIR(4)); // Yellow for in-progress messages
      mvwprintw(viewer->status_bar_win, 0, sync_text_pos, "%s", sync_text);
      wattroff(viewer->status_bar_win, COLOR_PAIR(4));
    }
  }

  wrefresh(viewer->status_bar_win);

  // Ensure cursor stays hidden and positioned off-screen
  move(viewer->terminal_height - 1, viewer->terminal_width - 1);
  refresh();
}

/**
 * Update sync status and check for new files
 */
void update_sync_status(NCursesDiffViewer *viewer) {
  if (!viewer)
    return;

  time_t current_time = time(NULL);

  // Check if it's time to sync (every 30 seconds)
  if (current_time - viewer->last_sync_time >= 30 &&
      !viewer->critical_operation_in_progress && !viewer->fetch_in_progress) {
    viewer->last_sync_time = current_time;

    // Start background fetch instead of blocking
    start_background_fetch(viewer);
    return;
  }

  // Always check if background fetch is complete
  check_background_fetch(viewer);

  // Handle all animation states
  if (viewer->sync_status != SYNC_STATUS_IDLE) {
    viewer->animation_frame++;

    // Handle fetching animation (4 seconds total: appear + visible + disappear)
    if (viewer->sync_status >= SYNC_STATUS_SYNCING_APPEARING &&
        viewer->sync_status <= SYNC_STATUS_SYNCING_DISAPPEARING) {
      if (viewer->sync_status == SYNC_STATUS_SYNCING_APPEARING) {
        // Appearing: one character every 2 frames (0.1s) for "Fetching" (8
        // chars)
        viewer->text_char_count = viewer->animation_frame / 2;
        if (viewer->text_char_count >= 8) {
          viewer->text_char_count = 8;
          viewer->sync_status = SYNC_STATUS_SYNCING_VISIBLE;
          viewer->animation_frame = 0;
        }
      } else if (viewer->sync_status == SYNC_STATUS_SYNCING_VISIBLE) {
        // Visible with spinner for 2.4 seconds (48 frames)
        if (viewer->animation_frame >= 48) {
          viewer->sync_status = SYNC_STATUS_SYNCING_DISAPPEARING;
          viewer->animation_frame = 0;
          viewer->text_char_count = 8;
        }
      } else if (viewer->sync_status == SYNC_STATUS_SYNCING_DISAPPEARING) {
        // Disappearing: remove one character every 2 frames (0.1s)
        int chars_to_remove = viewer->animation_frame / 2;
        viewer->text_char_count = 8 - chars_to_remove;
        if (viewer->text_char_count <= 0) {
          viewer->text_char_count = 0;
          viewer->sync_status = SYNC_STATUS_SYNCED_APPEARING;
          viewer->animation_frame = 0;
        }
      }
    }
    // Handle pushing animation (same 4 second pattern)
    else if (viewer->sync_status >= SYNC_STATUS_PUSHING_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_PUSHING_DISAPPEARING) {
      if (viewer->sync_status == SYNC_STATUS_PUSHING_APPEARING) {
        // Appearing: one character every frame (0.05s) for "Pushing" (7
        // chars)
        viewer->text_char_count = viewer->animation_frame;
        if (viewer->text_char_count >= 7) {
          viewer->text_char_count = 7;
          viewer->sync_status = SYNC_STATUS_PUSHING_VISIBLE;
          viewer->animation_frame = 0;
        }
      } else if (viewer->sync_status == SYNC_STATUS_PUSHING_VISIBLE) {
        // Visible with spinner - keep spinning until push_commit changes status
        // Don't auto-transition, let push_commit function handle the transition
        // This allows the spinner to keep going during the blocking git push
      } else if (viewer->sync_status == SYNC_STATUS_PUSHING_DISAPPEARING) {
        // Disappearing: remove one character every frame (0.05s)
        int chars_to_remove = viewer->animation_frame;
        viewer->text_char_count = 7 - chars_to_remove;
        if (viewer->text_char_count <= 0) {
          viewer->text_char_count = 0;
          viewer->sync_status = SYNC_STATUS_PUSHED_APPEARING;
          viewer->animation_frame = 0;
        }
      }
    }
    // Handle pulling animation (same pattern as pushing)
    else if (viewer->sync_status >= SYNC_STATUS_PULLING_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_PULLING_DISAPPEARING) {
      if (viewer->sync_status == SYNC_STATUS_PULLING_APPEARING) {
        // Appearing: one character every 2 frames (0.1s) for "Pulling" (7
        // chars)
        viewer->text_char_count = viewer->animation_frame / 2;
        if (viewer->text_char_count >= 7) {
          viewer->text_char_count = 7;
          viewer->sync_status = SYNC_STATUS_PULLING_VISIBLE;
          viewer->animation_frame = 0;
        }
      } else if (viewer->sync_status == SYNC_STATUS_PULLING_VISIBLE) {
        // Visible with spinner for 1.2 seconds (24 frames) - faster
        if (viewer->animation_frame >= 24) {
          viewer->sync_status = SYNC_STATUS_PULLING_DISAPPEARING;
          viewer->animation_frame = 0;
          viewer->text_char_count = 7;
        }
      } else if (viewer->sync_status == SYNC_STATUS_PULLING_DISAPPEARING) {
        // Disappearing: remove one character every 2 frames (0.1s)
        int chars_to_remove = viewer->animation_frame / 2;
        viewer->text_char_count = 7 - chars_to_remove;
        if (viewer->text_char_count <= 0) {
          viewer->text_char_count = 0;
          viewer->sync_status = SYNC_STATUS_PULLED_APPEARING;
          viewer->animation_frame = 0;
        }
      }
    }
    // Handle synced animation
    else if (viewer->sync_status >= SYNC_STATUS_SYNCED_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_SYNCED_DISAPPEARING) {
      if (viewer->sync_status == SYNC_STATUS_SYNCED_APPEARING) {
        // Appearing: one character every 2 frames (0.1s) for "Synced!" (7
        // chars)
        viewer->text_char_count = viewer->animation_frame / 2;
        if (viewer->text_char_count >= 7) {
          viewer->text_char_count = 7;
          viewer->sync_status = SYNC_STATUS_SYNCED_VISIBLE;
          viewer->animation_frame = 0;
        }
      } else if (viewer->sync_status == SYNC_STATUS_SYNCED_VISIBLE) {
        // Visible for 3 seconds (60 frames)
        if (viewer->animation_frame >= 60) {
          viewer->sync_status = SYNC_STATUS_SYNCED_DISAPPEARING;
          viewer->animation_frame = 0;
          viewer->text_char_count = 7;
        }
      } else if (viewer->sync_status == SYNC_STATUS_SYNCED_DISAPPEARING) {
        // Disappearing: remove one character every 2 frames (0.1s)
        int chars_to_remove = viewer->animation_frame / 2;
        viewer->text_char_count = 7 - chars_to_remove;
        if (viewer->text_char_count <= 0) {
          viewer->text_char_count = 0;
          viewer->sync_status = SYNC_STATUS_IDLE;
        }
      }
    }
    // Handle pushed animation
    else if (viewer->sync_status >= SYNC_STATUS_PUSHED_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_PUSHED_DISAPPEARING) {
      if (viewer->sync_status == SYNC_STATUS_PUSHED_APPEARING) {
        // Appearing: one character every frame (0.05s) for "Pushed!" (7
        // chars)
        viewer->text_char_count = viewer->animation_frame;
        if (viewer->text_char_count >= 7) {
          viewer->text_char_count = 7;
          viewer->sync_status = SYNC_STATUS_PUSHED_VISIBLE;
          viewer->animation_frame = 0;
        }
      } else if (viewer->sync_status == SYNC_STATUS_PUSHED_VISIBLE) {
        // Visible for 2 seconds (100 frames at 20ms)
        if (viewer->animation_frame >= 100) {
          viewer->sync_status = SYNC_STATUS_PUSHED_DISAPPEARING;
          viewer->animation_frame = 0;
          viewer->text_char_count = 7;
        }
      } else if (viewer->sync_status == SYNC_STATUS_PUSHED_DISAPPEARING) {
        // Disappearing: remove one character every frame (0.05s)
        int chars_to_remove = viewer->animation_frame;
        viewer->text_char_count = 7 - chars_to_remove;
        if (viewer->text_char_count <= 0) {
          viewer->text_char_count = 0;
          viewer->sync_status = SYNC_STATUS_IDLE;
        }
      }
    }
    // Handle pulled animation
    else if (viewer->sync_status >= SYNC_STATUS_PULLED_APPEARING &&
             viewer->sync_status <= SYNC_STATUS_PULLED_DISAPPEARING) {
      if (viewer->sync_status == SYNC_STATUS_PULLED_APPEARING) {
        // Appearing: one character every 2 frames (0.1s) for "Pulled!" (7
        // chars)
        viewer->text_char_count = viewer->animation_frame / 2;
        if (viewer->text_char_count >= 7) {
          viewer->text_char_count = 7;
          viewer->sync_status = SYNC_STATUS_PULLED_VISIBLE;
          viewer->animation_frame = 0;
        }
      } else if (viewer->sync_status == SYNC_STATUS_PULLED_VISIBLE) {
        // Visible for 2 seconds (40 frames)
        if (viewer->animation_frame >= 40) {
          viewer->sync_status = SYNC_STATUS_PULLED_DISAPPEARING;
          viewer->animation_frame = 0;
          viewer->text_char_count = 7;
        }
      } else if (viewer->sync_status == SYNC_STATUS_PULLED_DISAPPEARING) {
        // Disappearing: remove one character every 2 frames (0.1s)
        int chars_to_remove = viewer->animation_frame / 2;
        viewer->text_char_count = 7 - chars_to_remove;
        if (viewer->text_char_count <= 0) {
          viewer->text_char_count = 0;
          viewer->sync_status = SYNC_STATUS_IDLE;
        }
      }
    }
  }

  // Always update spinner frame for spinner animation
  viewer->spinner_frame++;
  if (viewer->spinner_frame > 100)
    viewer->spinner_frame = 0; // Reset to prevent overflow

  // Handle branch-specific animations (only for completed states)
  if (viewer->branch_push_status != SYNC_STATUS_IDLE ||
      viewer->branch_pull_status != SYNC_STATUS_IDLE) {
    viewer->branch_animation_frame++;

    // Handle push animations (only "Pushed!" phase)
    if (viewer->branch_push_status >= SYNC_STATUS_PUSHED_APPEARING &&
        viewer->branch_push_status <= SYNC_STATUS_PUSHED_DISAPPEARING) {
      if (viewer->branch_push_status == SYNC_STATUS_PUSHED_APPEARING) {
        viewer->branch_text_char_count = viewer->branch_animation_frame;
        if (viewer->branch_text_char_count >= 7) {
          viewer->branch_text_char_count = 7;
          viewer->branch_push_status = SYNC_STATUS_PUSHED_VISIBLE;
          viewer->branch_animation_frame = 0;
        }
      } else if (viewer->branch_push_status == SYNC_STATUS_PUSHED_VISIBLE) {
        if (viewer->branch_animation_frame >= 100) {
          viewer->branch_push_status = SYNC_STATUS_PUSHED_DISAPPEARING;
          viewer->branch_animation_frame = 0;
          viewer->branch_text_char_count = 7;
        }
      } else if (viewer->branch_push_status ==
                 SYNC_STATUS_PUSHED_DISAPPEARING) {
        int chars_to_remove = viewer->branch_animation_frame;
        viewer->branch_text_char_count = 7 - chars_to_remove;
        if (viewer->branch_text_char_count <= 0) {
          viewer->branch_text_char_count = 0;
          viewer->branch_push_status = SYNC_STATUS_IDLE;
          viewer->pushing_branch_index = -1;
        }
      }
    }

    // Handle pull animations (only "Pulled!" phase)
    if (viewer->branch_pull_status >= SYNC_STATUS_PULLED_APPEARING &&
        viewer->branch_pull_status <= SYNC_STATUS_PULLED_DISAPPEARING) {
      if (viewer->branch_pull_status == SYNC_STATUS_PULLED_APPEARING) {
        viewer->branch_text_char_count = viewer->branch_animation_frame / 2;
        if (viewer->branch_text_char_count >= 7) {
          viewer->branch_text_char_count = 7;
          viewer->branch_pull_status = SYNC_STATUS_PULLED_VISIBLE;
          viewer->branch_animation_frame = 0;
        }
      } else if (viewer->branch_pull_status == SYNC_STATUS_PULLED_VISIBLE) {
        if (viewer->branch_animation_frame >= 40) {
          viewer->branch_pull_status = SYNC_STATUS_PULLED_DISAPPEARING;
          viewer->branch_animation_frame = 0;
          viewer->branch_text_char_count = 7;
        }
      } else if (viewer->branch_pull_status ==
                 SYNC_STATUS_PULLED_DISAPPEARING) {
        int chars_to_remove = viewer->branch_animation_frame / 2;
        viewer->branch_text_char_count = 7 - chars_to_remove;
        if (viewer->branch_text_char_count <= 0) {
          viewer->branch_text_char_count = 0;
          viewer->branch_pull_status = SYNC_STATUS_IDLE;
          viewer->pulling_branch_index = -1;
        }
      }
    }
  }
}

int commit_staged_changes_only(NCursesDiffViewer *viewer,
                               const char *commit_title,
                               const char *commit_message) {
  if (!viewer || !commit_title || strlen(commit_title) == 0)
    return 0;

  char temp_msg_file[256];
  snprintf(temp_msg_file, sizeof(temp_msg_file), "/tmp/commit_msg_%d",
           getpid());

  FILE *msg_file = fopen(temp_msg_file, "w");
  if (!msg_file)
    return 0;

  if (commit_message && strlen(commit_message) > 0) {
    fprintf(msg_file, "%s\n\n%s", commit_title, commit_message);
  } else {
    fprintf(msg_file, "%s", commit_title);
  }
  fclose(msg_file);

  char commit_cmd[512];
  snprintf(commit_cmd, sizeof(commit_cmd),
           "git commit -F \"%s\" 2>/dev/null >/dev/null", temp_msg_file);
  int result = system(commit_cmd);
  unlink(temp_msg_file);

  if (result == 0) {
    usleep(100000);

    get_ncurses_changed_files(viewer);
    get_commit_history(viewer);
    get_ncurses_git_branches(viewer);

    if (viewer->file_count == 0) {
      viewer->selected_file = 0;
      viewer->file_line_count = 0;
      viewer->file_scroll_offset = 0;
    } else if (viewer->selected_file >= viewer->file_count) {
      viewer->selected_file = viewer->file_count - 1;
    }
    return 1;
  }
  return 0;
}

/**
 * Handle keyboard input for navigation
 */
int handle_ncurses_diff_input(NCursesDiffViewer *viewer, int key) {
  if (!viewer)
    return 0;

  // Handle fuzzy search input first if active
  if (viewer->fuzzy_search_active) {
    return handle_fuzzy_search_input(viewer, key);
  }
  
  // Handle grep search input if active
  if (viewer->grep_search_active) {
    return handle_grep_search_input(viewer, key);
  }

  int max_lines_visible = viewer->terminal_height - 4;

  // Global quit commands
  if (key == 'q' || key == 'Q') {
    return 0; // Exit
  }

  // Global number key navigation
  switch (key) {
  case '1':
    viewer->current_mode = NCURSES_MODE_FILE_LIST;
    viewer->split_view_mode = 0; // Exit split view mode
    // Load the selected file content when switching to file list mode
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_file_with_staging_info(
          viewer, viewer->files[viewer->selected_file].filename);
    }
    break;
  case '2':
    viewer->split_view_mode = 0; // Exit split view mode
    // Switch to file view mode and load selected file
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_file_with_staging_info(
          viewer, viewer->files[viewer->selected_file].filename);
      viewer->current_mode = NCURSES_MODE_FILE_VIEW;
    }
    break;
  case '3':
    viewer->current_mode = NCURSES_MODE_BRANCH_LIST;
    viewer->split_view_mode = 0; // Exit split view mode
    // Load commits for the currently selected branch when entering branch mode
    if (viewer->branch_count > 0) {
      load_branch_commits(viewer,
                          viewer->branches[viewer->selected_branch].name);
      viewer->branch_commits_scroll_offset = 0;
    }
    break;
  case '4':
    viewer->current_mode = NCURSES_MODE_COMMIT_LIST;
    viewer->split_view_mode = 0; // Exit split view mode
    // Auto-preview the selected commit
    if (viewer->commit_count > 0) {
      load_commit_for_viewing(viewer,
                              viewer->commits[viewer->selected_commit].hash);
    }
    break;
  case '5':
    viewer->current_mode = NCURSES_MODE_STASH_LIST;
    viewer->split_view_mode = 0; // Exit split view mode
    // Auto-preview the selected stash
    if (viewer->stash_count > 0) {
      load_stash_for_viewing(viewer, viewer->selected_stash);
    }
    break;
  }

  if (viewer->current_mode == NCURSES_MODE_FILE_LIST) {
    // File list mode navigation
    switch (key) {
    case 27:    // ESC
      return 0; // Exit from file list mode

    case KEY_UP:
    case 'k':
      if (viewer->selected_file > 0) {
        viewer->selected_file--;
      }
      break;

    case KEY_DOWN:
    case 'j':
      if (viewer->selected_file < viewer->file_count - 1) {
        viewer->selected_file++;
      }
      break;

    case ' ': // Space - toggle file marking
      if (viewer->file_count > 0 &&
          viewer->selected_file < viewer->file_count) {
        toggle_file_mark(viewer, viewer->selected_file);
      }
      break;

    case 'a':
    case 'A': // Mark all files
      mark_all_files(viewer);
      break;

    case 's':
    case 'S':
      viewer->critical_operation_in_progress = 1;
      create_ncurses_git_stash(viewer);
      viewer->critical_operation_in_progress = 0;
      break;

    case 'c':
    case 'C': // Commit marked files
    {
      char commit_title[MAX_COMMIT_TITLE_LEN] = "";
      char commit_message[2048] = "";
      viewer->critical_operation_in_progress = 2;

      if (viewer->file_count > 0 &&
          viewer->selected_file < viewer->file_count &&
          viewer->files[viewer->selected_file].has_staged_changes) {
        if (get_commit_title_input(commit_title, MAX_COMMIT_TITLE_LEN,
                                   commit_message, sizeof(commit_message))) {
          commit_staged_changes_only(viewer, commit_title, commit_message);
        }
      } else {
        if (get_commit_title_input(commit_title, MAX_COMMIT_TITLE_LEN,
                                   commit_message, sizeof(commit_message))) {
          commit_marked_files(viewer, commit_title, commit_message);
        }
      }
      viewer->critical_operation_in_progress = 0;

      clear();
      refresh();

      render_file_list_window(viewer);
      render_file_content_window(viewer);
      render_commit_list_window(viewer);
      render_branch_list_window(viewer);
      render_stash_list_window(viewer);
      render_status_bar(viewer);

    } break;

    case '\t': // Tab - switch to commit list mode
      viewer->current_mode = NCURSES_MODE_COMMIT_LIST;
      break;

    case '/': // Forward slash - enter fuzzy search mode
      enter_fuzzy_search_mode(viewer);
      break;

    case '\n':
    case '\r':
    case KEY_ENTER:
      // Enter file view mode with staging and load the selected file
      if (viewer->file_count > 0 &&
          viewer->selected_file < viewer->file_count) {
        load_file_with_staging_info(
            viewer, viewer->files[viewer->selected_file].filename);
        viewer->current_mode = NCURSES_MODE_FILE_VIEW;
        viewer->split_view_mode = 1; // Enable split view for staging
        viewer->active_pane = 0;     // Start in unstaged pane
      }
      break;
    }
  } else if (viewer->current_mode == NCURSES_MODE_FILE_VIEW) {
    // File view mode navigation
    switch (key) {
    case 27: // ESC
      if (viewer->split_view_mode) {
        viewer->split_view_mode = 0;                   // Exit split view
        viewer->current_mode = NCURSES_MODE_FILE_LIST; // Return to file list
      } else {
        viewer->current_mode = NCURSES_MODE_FILE_LIST;
      }
      break;

    case ' ': // Space - stage/unstage line
      if (viewer->split_view_mode) {
        if (viewer->active_pane == 0) {
          stage_hunk_by_line(viewer, viewer->file_cursor_line);
        } else {
          // Unstage from staged pane
          unstage_line_from_git(viewer, viewer->staged_cursor_line);
        }
      } else {
        // Existing scroll logic for non-split view
        if (viewer->file_line_count > max_lines_visible) {
          viewer->file_scroll_offset += max_lines_visible;
          if (viewer->file_scroll_offset >
              viewer->file_line_count - max_lines_visible) {
            viewer->file_scroll_offset =
                viewer->file_line_count - max_lines_visible;
          }
        }
      }
      break;

    case '\t': // Tab - switch between unstaged and staged panes
      if (viewer->split_view_mode) {
        viewer->active_pane = !viewer->active_pane;
        // Reset cursors when switching panes
        if (viewer->active_pane == 0) {
          viewer->file_cursor_line = 0;
        } else {
          viewer->staged_cursor_line = 0;
        }
      }
      break;

    case 'a': // apply staged changes
      if (viewer->split_view_mode) {
        apply_staged_changes(viewer);
        clear();
        refresh();
      }
      break;

    case 'r': // Reset staged changes
      if (viewer->split_view_mode) {
        reset_staged_changes(viewer);
      }
      break;

    case KEY_UP:
    case 'k':
      if (viewer->split_view_mode) {
        if (viewer->active_pane == 0) {
          // Unstaged pane
          move_cursor_smart_unstaged(viewer, -1);
        } else {
          // Staged pane
          move_cursor_smart_staged(viewer, -1);
        }
      } else {
        move_cursor_smart(viewer, -1);
      }
      break;

    case KEY_DOWN:
    case 'j':
      if (viewer->split_view_mode) {
        if (viewer->active_pane == 0) {
          // Unstaged pane
          move_cursor_smart_unstaged(viewer, 1);
        } else {
          // Staged pane
          move_cursor_smart_staged(viewer, 1);
        }
      } else {
        move_cursor_smart(viewer, 1);
      }
      break;

    case KEY_PPAGE: // Page Up
      // Scroll content up by page
      viewer->file_scroll_offset -= max_lines_visible;
      if (viewer->file_scroll_offset < 0) {
        viewer->file_scroll_offset = 0;
      }
      break;

    case 21: // Ctrl+U
    {
      // Move cursor up half page with smart positioning
      int target_cursor = viewer->file_cursor_line - max_lines_visible / 2;
      if (target_cursor < 0) {
        target_cursor = 0;
      }

      // Find the actual cursor position (skip empty lines like
      // move_cursor_smart does)
      int final_cursor = target_cursor;
      int attempts = 0;
      const int max_attempts = viewer->file_line_count;

      // Look for non-empty line near target position
      while (attempts < max_attempts &&
             final_cursor < viewer->file_line_count) {
        NCursesFileLine *line = &viewer->file_lines[final_cursor];
        char *trimmed = line->line;

        // Skip leading whitespace
        while (*trimmed == ' ' || *trimmed == '\t') {
          trimmed++;
        }

        // If line has content, stop here
        if (*trimmed != '\0') {
          break;
        }

        // Move down to find content, but don't go too far from target
        final_cursor++;
        attempts++;

        // If we've moved too far down, go back to target and try going up
        if (final_cursor > target_cursor + 5) {
          final_cursor = target_cursor;
          // Try going up from target
          while (final_cursor > 0 && attempts < max_attempts) {
            line = &viewer->file_lines[final_cursor];
            trimmed = line->line;
            while (*trimmed == ' ' || *trimmed == '\t') {
              trimmed++;
            }
            if (*trimmed != '\0') {
              break;
            }
            final_cursor--;
            attempts++;
          }
          break;
        }
      }

      // Ensure final cursor is in bounds
      if (final_cursor < 0)
        final_cursor = 0;
      if (final_cursor >= viewer->file_line_count)
        final_cursor = viewer->file_line_count - 1;

      viewer->file_cursor_line = final_cursor;

      // Adjust scroll based on FINAL cursor position
      if (viewer->file_cursor_line < viewer->file_scroll_offset + 5) {
        viewer->file_scroll_offset = viewer->file_cursor_line - 5;
        if (viewer->file_scroll_offset < 0) {
          viewer->file_scroll_offset = 0;
        }
      }
      break;
    }

    case 4: // Ctrl+D
    {
      // Move cursor down half page with smart positioning
      int target_cursor = viewer->file_cursor_line + max_lines_visible / 2;
      if (target_cursor >= viewer->file_line_count) {
        target_cursor = viewer->file_line_count - 1;
      }

      // Find the actual cursor position (skip empty lines like
      // move_cursor_smart does)
      int final_cursor = target_cursor;
      int attempts = 0;
      const int max_attempts = viewer->file_line_count;

      // Look for non-empty line near target position
      while (attempts < max_attempts && final_cursor >= 0) {
        NCursesFileLine *line = &viewer->file_lines[final_cursor];
        char *trimmed = line->line;

        // Skip leading whitespace
        while (*trimmed == ' ' || *trimmed == '\t') {
          trimmed++;
        }

        // If line has content, stop here
        if (*trimmed != '\0') {
          break;
        }

        // Move up to find content, but don't go too far from target
        final_cursor--;
        attempts++;

        // If we've moved too far up, go back to target and try going down
        if (final_cursor < target_cursor - 5) {
          final_cursor = target_cursor;
          // Try going down from target
          while (final_cursor < viewer->file_line_count - 1 &&
                 attempts < max_attempts) {
            line = &viewer->file_lines[final_cursor];
            trimmed = line->line;
            while (*trimmed == ' ' || *trimmed == '\t') {
              trimmed++;
            }
            if (*trimmed != '\0') {
              break;
            }
            final_cursor++;
            attempts++;
          }
          break;
        }
      }

      // Ensure final cursor is in bounds
      if (final_cursor < 0)
        final_cursor = 0;
      if (final_cursor >= viewer->file_line_count)
        final_cursor = viewer->file_line_count - 1;

      viewer->file_cursor_line = final_cursor;

      // Adjust scroll based on FINAL cursor position
      if (viewer->file_cursor_line >=
          viewer->file_scroll_offset + max_lines_visible - 5) {
        viewer->file_scroll_offset =
            viewer->file_cursor_line - max_lines_visible + 5;
        if (viewer->file_scroll_offset >
            viewer->file_line_count - max_lines_visible) {
          viewer->file_scroll_offset =
              viewer->file_line_count - max_lines_visible;
        }
        if (viewer->file_scroll_offset < 0) {
          viewer->file_scroll_offset = 0;
        }
      }
      break;
    }

    case KEY_NPAGE: // Page Down
      // Scroll content down by page
      if (viewer->file_line_count > max_lines_visible) {
        viewer->file_scroll_offset += max_lines_visible;
        if (viewer->file_scroll_offset >
            viewer->file_line_count - max_lines_visible) {
          viewer->file_scroll_offset =
              viewer->file_line_count - max_lines_visible;
        }
      }
      break;
    }

  } else if (viewer->current_mode == NCURSES_MODE_COMMIT_LIST) {

    // Commit list mode navigation
    switch (key) {
    case 27:   // ESC
    case '\t': // Tab - return to file list mode
      viewer->current_mode = NCURSES_MODE_FILE_LIST;
      viewer->split_view_mode = 0; // Exit split view mode
      break;

    case KEY_UP:
    case 'k':
      if (viewer->selected_commit > 0) {
        viewer->selected_commit--;
        // Auto-preview the selected commit
        if (viewer->commit_count > 0) {
          load_commit_for_viewing(
              viewer, viewer->commits[viewer->selected_commit].hash);
        }
      }
      break;

    case KEY_DOWN:
    case 'j':
      if (viewer->selected_commit < viewer->commit_count - 1) {
        viewer->selected_commit++;
        // Auto-preview the selected commit
        if (viewer->commit_count > 0) {
          load_commit_for_viewing(
              viewer, viewer->commits[viewer->selected_commit].hash);
        }
      }
      break;

    case '\n':
    case '\r':
    case KEY_ENTER:
      // Enter commit view mode
      if (viewer->commit_count > 0 &&
          viewer->selected_commit < viewer->commit_count) {
        load_commit_for_viewing(viewer,
                                viewer->commits[viewer->selected_commit].hash);
        viewer->current_mode = NCURSES_MODE_COMMIT_VIEW;
      }
      break;

    case 'P': // Push commit
      if (viewer->commit_count > 0 &&
          viewer->selected_commit < viewer->commit_count) {
        viewer->critical_operation_in_progress =
            1; // Block fetching during push
        // Show immediate feedback
        viewer->sync_status = SYNC_STATUS_PUSHING_VISIBLE;
        viewer->animation_frame = 0;
        viewer->text_char_count = 7; // Show full "Pushing" immediately
        // Force a quick render to show "Pushing!" immediately
        render_status_bar(viewer);
        wrefresh(viewer->status_bar_win);
        // Now do the actual push
        push_commit(viewer, viewer->selected_commit);
        viewer->critical_operation_in_progress = 0; // Re-enable fetching
      }
      break;

    case 'r':
      if (viewer->commit_count > 0 && viewer->selected_commit == 0) {
        viewer->critical_operation_in_progress = 1;
        reset_commit_soft(viewer, viewer->selected_commit);
        viewer->critical_operation_in_progress = 0;
      }
      break;

    case 'R': // Reset (hard) - undo commit and discard changes
      if (viewer->commit_count > 0 && viewer->selected_commit == 0) {
        viewer->critical_operation_in_progress = 1;
        reset_commit_hard(viewer, viewer->selected_commit);
        viewer->critical_operation_in_progress = 0;
      }
      break;

    case 'a':
    case 'A': // Amend most recent commit
      if (viewer->commit_count > 0) {
        viewer->critical_operation_in_progress = 1;
        amend_commit(viewer);
        viewer->critical_operation_in_progress = 0;

        // Force complete screen refresh after amend dialog
        clear();
        refresh();

        // Redraw all windows immediately
        render_file_list_window(viewer);
        render_file_content_window(viewer);
        render_commit_list_window(viewer);
        render_branch_list_window(viewer);
        render_stash_list_window(viewer);
        render_status_bar(viewer);
      }
      break;
      
    case '/': // Forward slash - enter grep search mode
      enter_grep_search_mode(viewer);
      break;
    }
  } else if (viewer->current_mode == NCURSES_MODE_STASH_LIST) {
    // Stash list mode navigation
    switch (key) {
    case 27:   // ESC
    case '\t': // Tab - return to file list mode
      viewer->current_mode = NCURSES_MODE_FILE_LIST;
      viewer->split_view_mode = 0; // Exit split view mode
      break;

    case KEY_UP:
    case 'k':
      if (viewer->selected_stash > 0) {
        viewer->selected_stash--;
        // Auto-preview the selected stash
        if (viewer->stash_count > 0) {
          load_stash_for_viewing(viewer, viewer->selected_stash);
        }
      }
      break;

    case KEY_DOWN:
    case 'j':
      if (viewer->selected_stash < viewer->stash_count - 1) {
        viewer->selected_stash++;
        // Auto-preview the selected stash
        if (viewer->stash_count > 0) {
          load_stash_for_viewing(viewer, viewer->selected_stash);
        }
      }
      break;

    case '\n':
    case '\r':
    case KEY_ENTER:
      // Enter stash view mode
      if (viewer->stash_count > 0 &&
          viewer->selected_stash < viewer->stash_count) {
        load_stash_for_viewing(viewer, viewer->selected_stash);
        viewer->current_mode = NCURSES_MODE_STASH_VIEW;
      }
      break;

    case ' ': // Space - Apply stash (keeps stash in list)
      if (viewer->stash_count > 0 &&
          viewer->selected_stash < viewer->stash_count) {
        viewer->critical_operation_in_progress = 1;
        if (apply_git_stash(viewer->selected_stash)) {
          // Refresh everything after applying stash
          get_ncurses_changed_files(viewer);
          get_commit_history(viewer);

          // Reset file selection if no files remain
          if (viewer->file_count == 0) {
            viewer->selected_file = 0;
            viewer->file_line_count = 0;
            viewer->file_scroll_offset = 0;
          } else if (viewer->selected_file >= viewer->file_count) {
            viewer->selected_file = viewer->file_count - 1;
          }

          // Reload current file if any files exist
          if (viewer->file_count > 0 &&
              viewer->selected_file < viewer->file_count) {
            load_file_with_staging_info(
                viewer, viewer->files[viewer->selected_file].filename);
          }
        }
        viewer->critical_operation_in_progress = 0;
      }
      break;

    case 'g':
    case 'G': // Pop stash (applies and removes from list)
      if (viewer->stash_count > 0 &&
          viewer->selected_stash < viewer->stash_count) {
        viewer->critical_operation_in_progress = 1;
        if (pop_git_stash(viewer->selected_stash)) {
          // Refresh everything after popping stash
          get_ncurses_changed_files(viewer);
          get_ncurses_git_stashes(viewer);
          get_commit_history(viewer);

          // Adjust selected stash if needed
          if (viewer->selected_stash >= viewer->stash_count &&
              viewer->stash_count > 0) {
            viewer->selected_stash = viewer->stash_count - 1;
          }

          // Reset file selection if no files remain
          if (viewer->file_count == 0) {
            viewer->selected_file = 0;
            viewer->file_line_count = 0;
            viewer->file_scroll_offset = 0;
          } else if (viewer->selected_file >= viewer->file_count) {
            viewer->selected_file = viewer->file_count - 1;
          }

          // Reload current file if any files exist
          if (viewer->file_count > 0 &&
              viewer->selected_file < viewer->file_count) {
            load_file_with_staging_info(
                viewer, viewer->files[viewer->selected_file].filename);
          }
        }
        viewer->critical_operation_in_progress = 0;
      }
      break;

    case 'd':
    case 'D': // Drop stash (removes without applying)
      if (viewer->stash_count > 0 &&
          viewer->selected_stash < viewer->stash_count) {
        viewer->critical_operation_in_progress = 1;
        if (drop_git_stash(viewer->selected_stash)) {
          // Refresh stash list after dropping
          get_ncurses_git_stashes(viewer);

          // Adjust selected stash if needed
          if (viewer->selected_stash >= viewer->stash_count &&
              viewer->stash_count > 0) {
            viewer->selected_stash = viewer->stash_count - 1;
          }
        }
        viewer->critical_operation_in_progress = 0;
      }
      break;
      
    case '/': // Forward slash - enter grep search mode
      enter_grep_search_mode(viewer);
      break;
    }
  } else if (viewer->current_mode == NCURSES_MODE_BRANCH_LIST) {
    // Branch list mode navigation
    switch (key) {
    case 27:   // ESC
    case '\t': // Tab - return to file list mode
      viewer->current_mode = NCURSES_MODE_FILE_LIST;
      viewer->split_view_mode = 0; // Exit split view mode
      break;

    case KEY_UP:
    case 'k':
      if (viewer->selected_branch > 0) {
        viewer->selected_branch--;
        // Load commits for the newly selected branch and reset scroll
        if (viewer->branch_count > 0) {
          load_branch_commits(viewer,
                              viewer->branches[viewer->selected_branch].name);
          viewer->branch_commits_scroll_offset = 0;
        }
      }
      break;

    case KEY_DOWN:
    case 'j':
      if (viewer->selected_branch < viewer->branch_count - 1) {
        viewer->selected_branch++;
        // Load commits for the newly selected branch and reset scroll
        if (viewer->branch_count > 0) {
          load_branch_commits(viewer,
                              viewer->branches[viewer->selected_branch].name);
          viewer->branch_commits_scroll_offset = 0;
        }
      }
      break;

    case '\n':
    case '\r':
    case KEY_ENTER:
      // Enter - Switch to branch view mode to scroll commits
      if (viewer->branch_count > 0 &&
          viewer->selected_branch < viewer->branch_count) {
        // Load commits and parse them for navigation
        load_branch_commits(viewer,
                            viewer->branches[viewer->selected_branch].name);
        parse_branch_commits_to_lines(viewer);
        viewer->current_mode = NCURSES_MODE_BRANCH_VIEW;
      }
      break;

    case 'c': // c - Checkout selected branch
      if (viewer->branch_count > 0 &&
          viewer->selected_branch < viewer->branch_count) {
        viewer->critical_operation_in_progress = 1;
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "git checkout \"%s\" >/dev/null 2>&1",
                 viewer->branches[viewer->selected_branch].name);

        if (system(cmd) == 0) {
          // Refresh everything after branch switch
          get_ncurses_changed_files(viewer);
          get_commit_history(viewer);
          get_ncurses_git_branches(
              viewer); // Refresh branch list to update current branch

          // Reset file selection if no files remain
          if (viewer->file_count == 0) {
            viewer->selected_file = 0;
            viewer->file_line_count = 0;
            viewer->file_scroll_offset = 0;
          } else if (viewer->selected_file >= viewer->file_count) {
            viewer->selected_file = viewer->file_count - 1;
          }

          // Reload current file if any files exist
          if (viewer->file_count > 0 &&
              viewer->selected_file < viewer->file_count) {
            load_file_with_staging_info(
                viewer, viewer->files[viewer->selected_file].filename);
          }
        }
        viewer->critical_operation_in_progress = 0;

        // Force complete screen refresh after checkout
        clear();
        refresh();
      }
      break;

    case 'n': // New branch
    {
      viewer->critical_operation_in_progress = 1;
      char new_branch_name[256];
      if (get_branch_name_input(new_branch_name, sizeof(new_branch_name))) {
        if (create_git_branch(new_branch_name)) {
          // Refresh everything after creating new branch
          get_ncurses_changed_files(viewer);
          get_commit_history(viewer);
          get_ncurses_git_branches(viewer);

          // Find and select the new branch (need to look for the cleaned name
          // with dashes)
          char clean_branch_name[256];
          strncpy(clean_branch_name, new_branch_name,
                  sizeof(clean_branch_name) - 1);
          clean_branch_name[sizeof(clean_branch_name) - 1] = '\0';
          for (int j = 0; clean_branch_name[j] != '\0'; j++) {
            if (clean_branch_name[j] == ' ') {
              clean_branch_name[j] = '-';
            }
          }

          for (int i = 0; i < viewer->branch_count; i++) {
            if (strcmp(viewer->branches[i].name, clean_branch_name) == 0) {
              viewer->selected_branch = i;
              break;
            }
          }

          // Reset file selection if no files remain
          if (viewer->file_count == 0) {
            viewer->selected_file = 0;
            viewer->file_line_count = 0;
            viewer->file_scroll_offset = 0;
          } else if (viewer->selected_file >= viewer->file_count) {
            viewer->selected_file = viewer->file_count - 1;
          }

          // Reload current file if any files exist
          if (viewer->file_count > 0 &&
              viewer->selected_file < viewer->file_count) {
            load_file_with_staging_info(
                viewer, viewer->files[viewer->selected_file].filename);
          }
        }
      }

      // Force immediate branch window update
      werase(viewer->branch_list_win);
      render_branch_list_window(viewer);
      wrefresh(viewer->branch_list_win);

      // Clear screen artifacts from dialog
      clear();
      refresh();
      viewer->critical_operation_in_progress = 0;
    } break;

    case 'd': // Delete branch
      if (viewer->branch_count > 0 &&
          viewer->selected_branch < viewer->branch_count) {
        viewer->critical_operation_in_progress = 1;
        const char *branch_to_delete =
            viewer->branches[viewer->selected_branch].name;

        // Don't allow deleting the current branch
        if (viewer->branches[viewer->selected_branch].status == 1) {
          show_error_popup("Cannot delete current branch!");
          break;
        }

        DeleteBranchOption option = show_delete_branch_dialog(branch_to_delete);
        if (option != DELETE_CANCEL) {
          if (delete_git_branch(branch_to_delete, option)) {
            // Refresh branch list after deletion
            get_ncurses_git_branches(viewer);

            // Adjust selection if needed
            if (viewer->selected_branch >= viewer->branch_count &&
                viewer->branch_count > 0) {
              viewer->selected_branch = viewer->branch_count - 1;
            }
          }
        }

        // Force immediate branch window update
        werase(viewer->branch_list_win);
        render_branch_list_window(viewer);
        wrefresh(viewer->branch_list_win);

        // Clear screen artifacts from dialog
        clear();
        refresh();
        viewer->critical_operation_in_progress = 0;
      }
      break;

    case 'r': // Rename branch
      if (viewer->branch_count > 0 &&
          viewer->selected_branch < viewer->branch_count) {
        viewer->critical_operation_in_progress = 1;
        const char *current_name =
            viewer->branches[viewer->selected_branch].name;
        char new_name[256];

        if (get_rename_branch_input(current_name, new_name, sizeof(new_name))) {
          if (rename_git_branch(current_name, new_name)) {
            // Refresh branch list after rename
            get_ncurses_git_branches(viewer);

            // Find and select the renamed branch
            for (int i = 0; i < viewer->branch_count; i++) {
              if (strcmp(viewer->branches[i].name, new_name) == 0) {
                viewer->selected_branch = i;
                break;
              }
            }
          }
        }

        // Force immediate branch window update
        werase(viewer->branch_list_win);
        render_branch_list_window(viewer);
        wrefresh(viewer->branch_list_win);

        // Clear screen artifacts from dialog
        clear();
        refresh();
        viewer->critical_operation_in_progress = 0;
      }
      break;

    case 'p':
      if (viewer->branch_count > 0 &&
          viewer->selected_branch < viewer->branch_count) {
        viewer->critical_operation_in_progress = 1;
        const char *branch_name =
            viewer->branches[viewer->selected_branch].name;

        if (viewer->branches[viewer->selected_branch].commits_behind > 0) {
          // Start pulling animation immediately
          viewer->sync_status = SYNC_STATUS_PULLING_APPEARING;
          viewer->animation_frame = 0;
          viewer->text_char_count = 0;

          // Set branch-specific pull status
          viewer->pulling_branch_index = viewer->selected_branch;
          viewer->branch_pull_status = SYNC_STATUS_PULLING_VISIBLE;
          viewer->branch_animation_frame = 0;
          viewer->branch_text_char_count = 7; // Show full "Pulling" immediately

          // Force immediate branch window refresh to show "Pulling" before the
          // blocking git operation
          werase(viewer->branch_list_win);
          render_branch_list_window(viewer);
          wrefresh(viewer->branch_list_win);

          // Create a simple animated pull with spinner updates
          pid_t pull_pid = fork();
          int result = 0;

          if (pull_pid == 0) {
            // Child process: do the actual pull
            exit(system("git pull 2>/dev/null >/dev/null"));
          }

          // Parent process: animate the spinner while pull is happening
          if (pull_pid > 0) {
            int status;
            int spinner_counter = 0;

            while (waitpid(pull_pid, &status, WNOHANG) == 0) {
              // Update spinner animation
              viewer->branch_animation_frame = spinner_counter;
              spinner_counter =
                  (spinner_counter + 1) % 40; // Cycle every 40 iterations

              // Refresh the branch window to show spinning animation
              werase(viewer->branch_list_win);
              render_branch_list_window(viewer);
              wrefresh(viewer->branch_list_win);

              // Small delay for animation timing
              usleep(100000); // 100ms delay
            }

            // Get the exit status of the pull command
            if (WIFEXITED(status)) {
              result = WEXITSTATUS(status);
            } else {
              result = 1; // Error
            }
          } else {
            result = 1; // Fork failed
          }

          if (result == 0) {
            // Reset branch animation completely
            viewer->branch_pull_status = SYNC_STATUS_PULLED_APPEARING;
            viewer->branch_animation_frame = 0;
            viewer->branch_text_char_count = 0;

            get_ncurses_changed_files(viewer);
            get_commit_history(viewer);
            get_ncurses_git_branches(viewer);
            if (viewer->file_count == 0) {
              viewer->selected_file = 0;
              viewer->file_line_count = 0;
              viewer->file_scroll_offset = 0;
            } else if (viewer->selected_file >= viewer->file_count) {
              viewer->selected_file = viewer->file_count - 1;
            }
            if (viewer->file_count > 0 &&
                viewer->selected_file < viewer->file_count) {
              load_file_with_staging_info(
                  viewer, viewer->files[viewer->selected_file].filename);
            }
            viewer->sync_status = SYNC_STATUS_PULLED_APPEARING;
            viewer->animation_frame = 0;
            viewer->text_char_count = 0;
          } else {
            show_error_popup("Pull failed. Check your network connection.");
            viewer->sync_status = SYNC_STATUS_IDLE;
            viewer->pulling_branch_index = -1;
            viewer->branch_pull_status = SYNC_STATUS_IDLE;
          }

        } else {
          show_error_popup("No commits to pull from remote");
        }
        viewer->critical_operation_in_progress = 0;
      }
      break;
      
    case '/': // Forward slash - enter grep search mode
      enter_grep_search_mode(viewer);
      break;
    }
  } else if (viewer->current_mode == NCURSES_MODE_COMMIT_VIEW) {
    // Commit view mode navigation (same as file view)
    switch (key) {
    case 27: // ESC
      viewer->current_mode =
          NCURSES_MODE_COMMIT_LIST; // Return to commit list mode
      viewer->split_view_mode = 0; // Exit split view mode
      break;

    case KEY_UP:
    case 'k':
      // Move cursor up while skipping empty lines
      move_cursor_smart(viewer, -1);
      break;

    case KEY_DOWN:
    case 'j':
      // Move cursor down while skipping empty lines
      move_cursor_smart(viewer, 1);
      break;

    case 21: // Ctrl+U
      // Move cursor up half page
      viewer->file_cursor_line -= max_lines_visible / 2;
      if (viewer->file_cursor_line < 0) {
        viewer->file_cursor_line = 0;
      }
      // Adjust scroll to keep cursor visible with padding
      if (viewer->file_cursor_line < viewer->file_scroll_offset + 3) {
        viewer->file_scroll_offset = viewer->file_cursor_line - 3;
        if (viewer->file_scroll_offset < 0) {
          viewer->file_scroll_offset = 0;
        }
      }
      break;

    case 4: // Ctrl+D
      // Move cursor down half page
      viewer->file_cursor_line += max_lines_visible / 2;
      if (viewer->file_cursor_line >= viewer->file_line_count) {
        viewer->file_cursor_line = viewer->file_line_count - 1;
      }
      // Adjust scroll to keep cursor visible with padding
      if (viewer->file_cursor_line >=
          viewer->file_scroll_offset + max_lines_visible - 3) {
        viewer->file_scroll_offset =
            viewer->file_cursor_line - max_lines_visible + 4;
        if (viewer->file_scroll_offset >
            viewer->file_line_count - max_lines_visible) {
          viewer->file_scroll_offset =
              viewer->file_line_count - max_lines_visible;
        }
        if (viewer->file_scroll_offset < 0) {
          viewer->file_scroll_offset = 0;
        }
      }
      break;

    case KEY_NPAGE: // Page Down
    case ' ':
      // Scroll content down by page
      if (viewer->file_line_count > max_lines_visible) {
        viewer->file_scroll_offset += max_lines_visible;
        if (viewer->file_scroll_offset >
            viewer->file_line_count - max_lines_visible) {
          viewer->file_scroll_offset =
              viewer->file_line_count - max_lines_visible;
        }
      }
      break;

    case KEY_PPAGE: // Page Up
      // Scroll content up by page
      viewer->file_scroll_offset -= max_lines_visible;
      if (viewer->file_scroll_offset < 0) {
        viewer->file_scroll_offset = 0;
      }
      break;
    }
  } else if (viewer->current_mode == NCURSES_MODE_STASH_VIEW) {
    // Stash view mode navigation (same as file view)
    switch (key) {
    case 27: // ESC
      viewer->current_mode =
          NCURSES_MODE_STASH_LIST; // Return to stash list mode
      viewer->split_view_mode = 0; // Exit split view mode
      break;

    case KEY_UP:
    case 'k':
      // Move cursor up while skipping empty lines
      move_cursor_smart(viewer, -1);
      break;

    case KEY_DOWN:
    case 'j':
      // Move cursor down while skipping empty lines
      move_cursor_smart(viewer, 1);
      break;

    case 21: // Ctrl+U
      // Move cursor up half page
      viewer->file_cursor_line -= max_lines_visible / 2;
      if (viewer->file_cursor_line < 0) {
        viewer->file_cursor_line = 0;
      }
      // Adjust scroll to keep cursor visible with padding
      if (viewer->file_cursor_line < viewer->file_scroll_offset + 3) {
        viewer->file_scroll_offset = viewer->file_cursor_line - 3;
        if (viewer->file_scroll_offset < 0) {
          viewer->file_scroll_offset = 0;
        }
      }
      break;

    case 4: // Ctrl+D
      // Move cursor down half page
      viewer->file_cursor_line += max_lines_visible / 2;
      if (viewer->file_cursor_line >= viewer->file_line_count) {
        viewer->file_cursor_line = viewer->file_line_count - 1;
      }
      // Adjust scroll to keep cursor visible with padding
      if (viewer->file_cursor_line >=
          viewer->file_scroll_offset + max_lines_visible - 3) {
        viewer->file_scroll_offset =
            viewer->file_cursor_line - max_lines_visible + 4;
        if (viewer->file_scroll_offset >
            viewer->file_line_count - max_lines_visible) {
          viewer->file_scroll_offset =
              viewer->file_line_count - max_lines_visible;
        }
        if (viewer->file_scroll_offset < 0) {
          viewer->file_scroll_offset = 0;
        }
      }
      break;

    case KEY_NPAGE: // Page Down
    case ' ':
      // Scroll content down by page
      if (viewer->file_line_count > max_lines_visible) {
        viewer->file_scroll_offset += max_lines_visible;
        if (viewer->file_scroll_offset >
            viewer->file_line_count - max_lines_visible) {
          viewer->file_scroll_offset =
              viewer->file_line_count - max_lines_visible;
        }
      }
      break;

    case KEY_PPAGE: // Page Up
      // Scroll content up by page
      viewer->file_scroll_offset -= max_lines_visible;
      if (viewer->file_scroll_offset < 0) {
        viewer->file_scroll_offset = 0;
      }
      break;
    }
  } else if (viewer->current_mode == NCURSES_MODE_BRANCH_VIEW) {
    // Branch view mode navigation (same as file view)
    switch (key) {
    case 27: // ESC
      viewer->current_mode =
          NCURSES_MODE_BRANCH_LIST; // Return to branch list mode
      viewer->split_view_mode = 0; // Exit split view mode
      break;

    case KEY_UP:
    case 'k':
      // Move cursor up while skipping empty lines
      move_cursor_smart(viewer, -1);
      break;

    case KEY_DOWN:
    case 'j':
      // Move cursor down while skipping empty lines
      move_cursor_smart(viewer, 1);
      break;

    case 21: // Ctrl+U
      // Move cursor up half page
      viewer->file_cursor_line -= max_lines_visible / 2;
      if (viewer->file_cursor_line < 0) {
        viewer->file_cursor_line = 0;
      }
      // Adjust scroll to keep cursor visible with padding
      if (viewer->file_cursor_line < viewer->file_scroll_offset + 3) {
        viewer->file_scroll_offset = viewer->file_cursor_line - 3;
        if (viewer->file_scroll_offset < 0) {
          viewer->file_scroll_offset = 0;
        }
      }
      break;

    case 4: // Ctrl+D
      // Move cursor down half page
      viewer->file_cursor_line += max_lines_visible / 2;
      if (viewer->file_cursor_line >= viewer->file_line_count) {
        viewer->file_cursor_line = viewer->file_line_count - 1;
      }
      // Adjust scroll to keep cursor visible with padding
      if (viewer->file_cursor_line >=
          viewer->file_scroll_offset + max_lines_visible - 3) {
        viewer->file_scroll_offset =
            viewer->file_cursor_line - max_lines_visible + 4;
        if (viewer->file_scroll_offset >
            viewer->file_line_count - max_lines_visible) {
          viewer->file_scroll_offset =
              viewer->file_line_count - max_lines_visible;
        }
        if (viewer->file_scroll_offset < 0) {
          viewer->file_scroll_offset = 0;
        }
      }
      break;

    case KEY_NPAGE: // Page Down
    case ' ':
      // Scroll content down by page
      if (viewer->file_line_count > max_lines_visible) {
        viewer->file_scroll_offset += max_lines_visible;
        if (viewer->file_scroll_offset >
            viewer->file_line_count - max_lines_visible) {
          viewer->file_scroll_offset =
              viewer->file_line_count - max_lines_visible;
        }
      }
      break;

    case KEY_PPAGE: // Page Up
      // Scroll content up by page
      viewer->file_scroll_offset -= max_lines_visible;
      if (viewer->file_scroll_offset < 0) {
        viewer->file_scroll_offset = 0;
      }
      break;
    }
  }

  return 1; // Continue
}

/**
 * Run the ncurses diff viewer
 */
int run_ncurses_diff_viewer(void) {
  NCursesDiffViewer viewer;

  if (!init_ncurses_diff_viewer(&viewer)) {
    printf("Failed to initialize ncurses diff viewer\n");
    return 1;
  }
  signal(SIGWINCH, handle_sigwinch);

  // Get changed files (can be 0, that's okay)
  get_ncurses_changed_files(&viewer);

  // get stashes
  get_ncurses_git_stashes(&viewer);

  // get branches
  get_ncurses_git_branches(&viewer);

  // Load commit history
  get_commit_history(&viewer);

  // Initial preview will be handled by update_preview_for_current_selection

  // Initial display
  attron(COLOR_PAIR(3));
  if (viewer.current_mode == NCURSES_MODE_FILE_LIST) {
    mvprintw(0, 0,
             "Git Diff Viewer: 1=files 2=view 3=branches 4=commits 5=stashes | "
             "j/k=nav "
             "Space=mark "
             "A=all S=stash C=commit P=push | q=quit");
  } else if (viewer.current_mode == NCURSES_MODE_FILE_VIEW) {
    mvprintw(0, 0,
             "Git Diff Viewer: 1=files 2=view 3=branches 4=commits 5=stashes | "
             "j/k=scroll "
             "Ctrl+U/D=30lines | q=quit");
  } else {
    mvprintw(0, 0,
             "Git Diff Viewer: 1=files 2=view 3=branches 4=commits 5=stashes | "
             "j/k=nav P=push "
             "r/R=reset a=amend | q=quit");
  }

  attroff(COLOR_PAIR(3));
  refresh();

  render_file_list_window(&viewer);
  render_file_content_window(&viewer);
  render_commit_list_window(&viewer);
  render_branch_list_window(&viewer);
  render_stash_list_window(&viewer);
  render_status_bar(&viewer);
  render_fuzzy_search(&viewer);
  render_grep_search(&viewer);

  // Main display loop
  int running = 1;
  NCursesViewMode last_mode = viewer.current_mode;

  while (running) {

    if (terminal_resized) {
      handle_terminal_resize(&viewer);
    }

    // Only update title if mode changed
    if (viewer.current_mode != last_mode) {
      // Clear just the title line
      move(0, 0);
      clrtoeol();

      attron(COLOR_PAIR(3));
      if (viewer.current_mode == NCURSES_MODE_FILE_LIST) {
        mvprintw(0, 0,
                 "Git Diff Viewer: 1=files 2=view 3=branches 4=commits "
                 "5=stashes | j/k=nav "
                 "Space=mark A=all S=stash C=commit P=push | q=quit");
      } else if (viewer.current_mode == NCURSES_MODE_FILE_VIEW) {
        mvprintw(0, 0,
                 "Git Diff Viewer: 1=files 2=view 3=branches 4=commits "
                 "5=stashes | j/k=scroll "
                 "Ctrl+U/D=30lines | q=quit");
      } else {
        mvprintw(
            0, 0,
            "Git Diff Viewer: 1=files 2=view 3=branches 4=commits 5=stashes | "
            "j/k=nav P=push "
            "r/R=reset a=amend | q=quit");
      }
      attroff(COLOR_PAIR(3));
      refresh();
      last_mode = viewer.current_mode;
    }
    // Update sync status and check for new files
    update_sync_status(&viewer);

    // Update preview based on current selection
    update_preview_for_current_selection(&viewer);

    // Skip main window rendering if fuzzy or grep search is active to prevent flickering
    if (!viewer.fuzzy_search_active && !viewer.grep_search_active) {
      render_file_list_window(&viewer);
      render_file_content_window(&viewer);
      render_commit_list_window(&viewer);
      render_branch_list_window(&viewer);
      render_stash_list_window(&viewer);
      render_status_bar(&viewer);
    }
    
    // Always render search overlays (they handle their own visibility)
    render_fuzzy_search(&viewer);
    render_grep_search(&viewer);

    // Keep cursor hidden
    curs_set(0);

    int c = getch();
    if (c != ERR) { // Only process if a key was actually pressed
      running = handle_ncurses_diff_input(&viewer, c);
    }

    // Small delay to prevent excessive CPU usage and allow animations
    usleep(20000); // 20ms delay
  }

  cleanup_ncurses_diff_viewer(&viewer);
  return 0;
}

/**
 * Get list of git stashes
 */

/**
 * Get list of git branches
 */

int get_ncurses_git_branches(NCursesDiffViewer *viewer) {
  if (!viewer)
    return 0;

  viewer->branch_count = 0;

  // Note: We now do fetching in the background to avoid UI freezes

  // Use git branch (without -a) to only show local branches
  FILE *fp = popen("git branch 2>/dev/null", "r");
  if (!fp) {
    return 0;
  }

  char line[512];
  while (fgets(line, sizeof(line), fp) && viewer->branch_count < MAX_BRANCHES) {
    // Remove newline
    line[strcspn(line, "\n")] = 0;

    // Skip empty lines
    if (strlen(line) == 0)
      continue;

    // Check if this is the current branch (starts with *)
    int is_current = 0;
    char *branch_name = line;

    // Skip leading spaces
    while (*branch_name == ' ')
      branch_name++;

    if (*branch_name == '*') {
      is_current = 1;
      branch_name++; // Skip the *
      while (*branch_name == ' ')
        branch_name++; // Skip spaces after *
    }

    // Skip any lines that contain "->" (symbolic references like origin/HEAD ->
    // origin/main)
    if (strstr(branch_name, "->") != NULL) {
      continue;
    }

    // Skip remote branches (shouldn't appear with git branch, but just in case)
    if (strncmp(branch_name, "remotes/", 8) == 0) {
      continue;
    }

    // Copy branch name
    strncpy(viewer->branches[viewer->branch_count].name, branch_name,
            MAX_BRANCHNAME_LEN - 1);
    viewer->branches[viewer->branch_count].name[MAX_BRANCHNAME_LEN - 1] = '\0';
    viewer->branches[viewer->branch_count].status = is_current;

    // Initialize ahead/behind counts
    viewer->branches[viewer->branch_count].commits_ahead = 0;
    viewer->branches[viewer->branch_count].commits_behind = 0;

    // Get ahead/behind status using the exact method lazygit uses
    // First check if remote branch exists
    char remote_exists_cmd[512];
    snprintf(remote_exists_cmd, sizeof(remote_exists_cmd),
             "git show-ref --verify --quiet refs/remotes/origin/%s",
             branch_name);

    if (system(remote_exists_cmd) == 0) {
      // Remote branch exists, now get ahead/behind counts

      // Get commits behind (how many commits remote has that we don't)
      char behind_cmd[512];
      snprintf(behind_cmd, sizeof(behind_cmd),
               "git rev-list --count %s..origin/%s 2>/dev/null", branch_name,
               branch_name);

      FILE *behind_fp = popen(behind_cmd, "r");
      if (behind_fp) {
        char behind_count[32];
        if (fgets(behind_count, sizeof(behind_count), behind_fp) != NULL) {
          viewer->branches[viewer->branch_count].commits_behind =
              atoi(behind_count);
        }
        pclose(behind_fp);
      }

      // Get commits ahead (how many commits we have that remote doesn't)
      char ahead_cmd[512];
      snprintf(ahead_cmd, sizeof(ahead_cmd),
               "git rev-list --count origin/%s..%s 2>/dev/null", branch_name,
               branch_name);

      FILE *ahead_fp = popen(ahead_cmd, "r");
      if (ahead_fp) {
        char ahead_count[32];
        if (fgets(ahead_count, sizeof(ahead_count), ahead_fp) != NULL) {
          viewer->branches[viewer->branch_count].commits_ahead =
              atoi(ahead_count);
        }
        pclose(ahead_fp);
      }
    }

    viewer->branch_count++;
  }

  pclose(fp);
  return 1;
}

/**
 * Get branch name input from user for new branch creation
 */
int get_branch_name_input(char *branch_name, int max_len) {
  if (!branch_name || max_len <= 0)
    return 0;

  // Create input window
  int win_height = 7;
  int win_width = 60;
  int start_y = (LINES - win_height) / 2;
  int start_x = (COLS - win_width) / 2;

  WINDOW *input_win = newwin(win_height, win_width, start_y, start_x);
  if (!input_win)
    return 0;

  char input[256] = {0};
  int input_pos = 0;
  int result = 0;

  // Enable echo and cursor for input
  echo();
  curs_set(1);
  keypad(input_win, TRUE);

  while (1) {
    // Redraw window
    werase(input_win);
    box(input_win, 0, 0);
    mvwprintw(input_win, 0, 2, " Create New Branch ");
    mvwprintw(input_win, 2, 2, "Branch name:");
    mvwprintw(input_win, 5, 2, "Enter: create | Esc: cancel");

    // Show current input
    mvwprintw(input_win, 3, 2, "> %s", input);

    // Position cursor at end of input
    wmove(input_win, 3, 4 + input_pos);
    wrefresh(input_win);

    int ch = wgetch(input_win);

    switch (ch) {
    case 27: // ESC
      result = 0;
      goto cleanup;

    case '\n':
    case '\r':
    case KEY_ENTER:
      if (input_pos > 0) {
        strncpy(branch_name, input, max_len - 1);
        branch_name[max_len - 1] = '\0';
        result = 1;
      }
      goto cleanup;

    case KEY_BACKSPACE:
    case 127:
    case '\b':
      if (input_pos > 0) {
        input_pos--;
        input[input_pos] = '\0';
      }
      break;

    default:
      // Add printable characters
      if (ch >= 32 && ch <= 126 && input_pos < max_len - 1) {
        input[input_pos] = ch;
        input_pos++;
        input[input_pos] = '\0';
      }
      break;
    }
  }

cleanup:
  // Restore ncurses settings
  noecho();
  curs_set(0);
  delwin(input_win);

  return result;
}

/**
 * Create a new git branch
 */
int create_git_branch(const char *branch_name) {
  if (!branch_name || strlen(branch_name) == 0)
    return 0;

  // Clean branch name: replace spaces with dashes
  char clean_branch_name[256];
  strncpy(clean_branch_name, branch_name, sizeof(clean_branch_name) - 1);
  clean_branch_name[sizeof(clean_branch_name) - 1] = '\0';

  // Replace spaces with dashes
  for (int i = 0; clean_branch_name[i] != '\0'; i++) {
    if (clean_branch_name[i] == ' ') {
      clean_branch_name[i] = '-';
    }
  }

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "git checkout -b \"%s\" >/dev/null 2>&1",
           clean_branch_name);

  return (system(cmd) == 0);
}

/**
 * Get new branch name input from user for branch renaming
 */
int get_rename_branch_input(const char *current_name, char *new_name,
                            int max_len) {
  if (!current_name || !new_name || max_len <= 0)
    return 0;

  // Create input window
  int win_height = 8;
  int win_width = 60;
  int start_y = (LINES - win_height) / 2;
  int start_x = (COLS - win_width) / 2;

  WINDOW *input_win = newwin(win_height, win_width, start_y, start_x);
  if (!input_win)
    return 0;

  char input[256] = {0};
  int input_pos = 0;
  int result = 0;

  // Pre-fill with current name
  strncpy(input, current_name, sizeof(input) - 1);
  input_pos = strlen(input);

  // Enable echo and cursor for input
  echo();
  curs_set(1);
  keypad(input_win, TRUE);

  while (1) {
    // Redraw window
    werase(input_win);
    box(input_win, 0, 0);
    mvwprintw(input_win, 0, 2, " Rename Branch ");
    mvwprintw(input_win, 2, 2, "Current: %s", current_name);
    mvwprintw(input_win, 3, 2, "New name:");
    mvwprintw(input_win, 6, 2, "Enter: rename | Esc: cancel");

    // Show current input
    mvwprintw(input_win, 4, 2, "> %s", input);

    // Position cursor at end of input
    wmove(input_win, 4, 4 + input_pos);
    wrefresh(input_win);

    int ch = wgetch(input_win);

    switch (ch) {
    case 27: // ESC
      result = 0;
      goto cleanup_rename;

    case '\n':
    case '\r':
    case KEY_ENTER:
      if (input_pos > 0 && strcmp(input, current_name) != 0) {
        strncpy(new_name, input, max_len - 1);
        new_name[max_len - 1] = '\0';
        result = 1;
      }
      goto cleanup_rename;

    case KEY_BACKSPACE:
    case 127:
    case '\b':
      if (input_pos > 0) {
        input_pos--;
        input[input_pos] = '\0';
      }
      break;

    default:
      // Add printable characters
      if (ch >= 32 && ch <= 126 && input_pos < max_len - 1) {
        input[input_pos] = ch;
        input_pos++;
        input[input_pos] = '\0';
      }
      break;
    }
  }

cleanup_rename:
  // Restore ncurses settings
  noecho();
  curs_set(0);
  delwin(input_win);

  return result;
}

/**
 * Rename a git branch
 */
int rename_git_branch(const char *old_name, const char *new_name) {
  if (!old_name || !new_name || strlen(old_name) == 0 || strlen(new_name) == 0)
    return 0;

  char cmd[512];
  snprintf(cmd, sizeof(cmd), "git branch -m \"%s\" \"%s\" >/dev/null 2>&1",
           old_name, new_name);

  return (system(cmd) == 0);
}

/**
 * Show delete branch confirmation dialog
 */
int show_delete_branch_dialog(const char *branch_name) {
  if (!branch_name)
    return DELETE_CANCEL;

  // Create dialog window
  int win_height = 8;
  int win_width = 50;
  int start_y = (LINES - win_height) / 2;
  int start_x = (COLS - win_width) / 2;

  WINDOW *dialog_win = newwin(win_height, win_width, start_y, start_x);
  if (!dialog_win)
    return DELETE_CANCEL;

  int selected_option = DELETE_LOCAL;
  const char *options[] = {"Delete local (l)", "Delete remote (r)",
                           "Delete both (b)"};

  while (1) {
    werase(dialog_win);
    box(dialog_win, 0, 0);
    mvwprintw(dialog_win, 0, 2, " Delete Branch ");
    mvwprintw(dialog_win, 2, 2, "Branch: %s", branch_name);

    // Draw options
    for (int i = 0; i < 3; i++) {
      int y = 3 + i;
      if (i == selected_option) {
        wattron(dialog_win, COLOR_PAIR(5)); // Highlight selected
        mvwprintw(dialog_win, y, 2, "> %s", options[i]);
        wattroff(dialog_win, COLOR_PAIR(5));
      } else {
        mvwprintw(dialog_win, y, 2, "  %s", options[i]);
      }
    }

    mvwprintw(dialog_win, 6, 2, "Enter: select | Esc: cancel");
    wrefresh(dialog_win);

    int key = getch();
    switch (key) {
    case 27: // ESC
      delwin(dialog_win);
      return DELETE_CANCEL;

    case 'l':
      delwin(dialog_win);
      return DELETE_LOCAL;

    case 'r':
      delwin(dialog_win);
      return DELETE_REMOTE;

    case 'b':
      delwin(dialog_win);
      return DELETE_BOTH;

    case KEY_UP:
    case 'k':
      if (selected_option > 0) {
        selected_option--;
      }
      break;

    case KEY_DOWN:
    case 'j':
      if (selected_option < 2) {
        selected_option++;
      }
      break;

    case '\n':
    case '\r':
    case KEY_ENTER:
      delwin(dialog_win);
      return selected_option;
    }
  }
}

/**
 * Show universal error popup
 */
void show_error_popup(const char *error_message) {
  if (!error_message)
    return;

  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);

  int popup_height = 5;
  int popup_width = strlen(error_message) + 6;
  if (popup_width > max_x - 4) {
    popup_width = max_x - 4;
  }

  int start_y = (max_y - popup_height) / 2;
  int start_x = (max_x - popup_width) / 2;

  WINDOW *popup_win = newwin(popup_height, popup_width, start_y, start_x);

  wattron(popup_win, COLOR_PAIR(1));
  box(popup_win, 0, 0);

  mvwprintw(popup_win, 1, 2, "Error:");
  mvwprintw(popup_win, 2, 2, "%.*s", popup_width - 4, error_message);
  mvwprintw(popup_win, 3, 2, "Press any key to continue...");

  wattroff(popup_win, COLOR_PAIR(1));
  wrefresh(popup_win);

  // Wait for user input
  getch();

  delwin(popup_win);
  clear();
  refresh();
}

/**
 * Get available git remotes
 */
int get_git_remotes(char remotes[][256], int max_remotes) {
  FILE *fp = popen("git remote 2>/dev/null", "r");
  if (!fp)
    return 0;

  int count = 0;
  char line[256];

  while (fgets(line, sizeof(line), fp) && count < max_remotes) {
    // Remove trailing newline
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
    }

    if (strlen(line) > 0) {
      strncpy(remotes[count], line, 255);
      remotes[count][255] = '\0';
      count++;
    }
  }

  pclose(fp);
  return count;
}

/**
 * Show upstream selection dialog
 */
int show_upstream_selection_dialog(const char *branch_name,
                                   char *upstream_result, int max_len) {
  if (!branch_name || !upstream_result)
    return 0;

  int max_y, max_x;
  getmaxyx(stdscr, max_y, max_x);

  int dialog_height = 12;
  int dialog_width = 60;
  int start_y = (max_y - dialog_height) / 2;
  int start_x = (max_x - dialog_width) / 2;

  WINDOW *dialog_win = newwin(dialog_height, dialog_width, start_y, start_x);

  // Get available remotes
  char remotes[10][256];
  int remote_count = get_git_remotes(remotes, 10);

  char input_buffer[256] = "";
  int cursor_pos = 0;
  int selected_suggestion = 0;

  // Default suggestion
  if (remote_count > 0) {
    snprintf(input_buffer, sizeof(input_buffer), "%s %s", remotes[0],
             branch_name);
    cursor_pos = strlen(input_buffer);
  }

  while (1) {
    werase(dialog_win);
    box(dialog_win, 0, 0);

    // Title
    mvwprintw(dialog_win, 1, 2, "Set Upstream Branch");
    mvwprintw(dialog_win, 2, 2, "Enter upstream as <remote> <branchname>");

    // Input field
    mvwprintw(dialog_win, 4, 2, "Upstream: %s", input_buffer);

    // Suggestions header
    mvwprintw(dialog_win, 6, 2, "Suggestions (press <tab> to focus):");

    // Show suggestions
    for (int i = 0; i < remote_count && i < 3; i++) {
      char suggestion[256];
      snprintf(suggestion, sizeof(suggestion), "%s %s", remotes[i],
               branch_name);

      if (i == selected_suggestion) {
        wattron(dialog_win, A_REVERSE);
      }
      mvwprintw(dialog_win, 7 + i, 4, "%s", suggestion);
      if (i == selected_suggestion) {
        wattroff(dialog_win, A_REVERSE);
      }
    }

    // Instructions
    mvwprintw(dialog_win, dialog_height - 2, 2, "Enter: Set | Esc: Cancel");

    wrefresh(dialog_win);

    int key = getch();

    switch (key) {
    case 27: // ESC
      delwin(dialog_win);
      return 0;

    case '\n':
    case '\r':
    case KEY_ENTER:
      if (strlen(input_buffer) > 0) {
        strncpy(upstream_result, input_buffer, max_len - 1);
        upstream_result[max_len - 1] = '\0';
        delwin(dialog_win);
        return 1;
      }
      break;

    case '\t':
      // Tab to select suggestion
      if (remote_count > 0 && selected_suggestion < remote_count) {
        snprintf(input_buffer, sizeof(input_buffer), "%s %s",
                 remotes[selected_suggestion], branch_name);
        cursor_pos = strlen(input_buffer);
      }
      break;

    case KEY_UP:
      if (selected_suggestion > 0) {
        selected_suggestion--;
      }
      break;

    case KEY_DOWN:
      if (selected_suggestion < remote_count - 1) {
        selected_suggestion++;
      }
      break;

    case KEY_BACKSPACE:
    case 127:
      if (cursor_pos > 0) {
        cursor_pos--;
        input_buffer[cursor_pos] = '\0';
      }
      break;

    default:
      if (isprint(key) && cursor_pos < (int)sizeof(input_buffer) - 1) {
        input_buffer[cursor_pos] = key;
        cursor_pos++;
        input_buffer[cursor_pos] = '\0';
      }
      break;
    }
  }

  delwin(dialog_win);
  return 0;
}

/**
 * Get current git branch name
 */
int get_current_branch_name(char *branch_name, int max_len) {
  if (!branch_name)
    return 0;

  FILE *fp = popen("git rev-parse --abbrev-ref HEAD 2>/dev/null", "r");
  if (!fp)
    return 0;

  if (fgets(branch_name, max_len, fp) != NULL) {
    // Remove trailing newline
    int len = strlen(branch_name);
    if (len > 0 && branch_name[len - 1] == '\n') {
      branch_name[len - 1] = '\0';
    }
    pclose(fp);
    return 1;
  }

  pclose(fp);
  return 0;
}

/**
 * Check if a branch has an upstream
 */
int branch_has_upstream(const char *branch_name) {
  if (!branch_name)
    return 0;

  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "git rev-parse --abbrev-ref \"%s@{upstream}\" >/dev/null 2>&1",
           branch_name);
  return (system(cmd) == 0);
}

/**
 * Delete git branch based on option
 */
int delete_git_branch(const char *branch_name, DeleteBranchOption option) {
  if (!branch_name || option == DELETE_CANCEL)
    return 0;

  // Check for upstream before attempting remote deletion
  if (option == DELETE_REMOTE || option == DELETE_BOTH) {
    if (!branch_has_upstream(branch_name)) {
      // Show error message for branches without upstream
      show_error_popup("The selected branch has no upstream (tip: delete the "
                       "branch locally)");
      return 0; // Don't proceed with deletion
    }
  }

  char cmd[512];
  int success = 1;

  switch (option) {
  case DELETE_LOCAL:
    snprintf(cmd, sizeof(cmd), "git branch -D \"%s\" >/dev/null 2>&1",
             branch_name);
    success = (system(cmd) == 0);
    break;

  case DELETE_REMOTE:
    snprintf(cmd, sizeof(cmd),
             "git push origin --delete \"%s\" >/dev/null 2>&1", branch_name);
    success = (system(cmd) == 0);
    break;

  case DELETE_BOTH:
    // Delete local first
    snprintf(cmd, sizeof(cmd), "git branch -D \"%s\" >/dev/null 2>&1",
             branch_name);
    success = (system(cmd) == 0);

    // Then delete remote
    if (success) {
      snprintf(cmd, sizeof(cmd),
               "git push origin --delete \"%s\" >/dev/null 2>&1", branch_name);
      success = (system(cmd) == 0);
    }
    break;

  case DELETE_CANCEL:
  default:
    return 0;
  }

  return success;
}

int get_ncurses_git_stashes(NCursesDiffViewer *viewer) {
  if (!viewer)
    return 0;

  char stash_lines[MAX_STASHES][512];
  viewer->stash_count = get_git_stashes(stash_lines, MAX_STASHES);

  for (int i = 0; i < viewer->stash_count; i++) {
    strncpy(viewer->stashes[i].stash_info, stash_lines[i], 511);
    viewer->stashes[i].stash_info[511] = '\0';
  }

  return viewer->stash_count;
}

/**
 * Get stash name input from user
 */
int get_stash_name_input(char *stash_name, int max_len) {
  if (!stash_name)
    return 0;

  // Save current screen
  WINDOW *saved_screen = dupwin(stdscr);

  // Calculate window dimensions
  int input_width = COLS * 0.6; // 60% of screen width
  int input_height = 3;
  int start_x = COLS / 2 - input_width / 2;
  int start_y = LINES / 2 - input_height / 2;

  // Create input window
  WINDOW *input_win = newwin(input_height, input_width, start_y, start_x);

  if (!input_win) {
    if (saved_screen)
      delwin(saved_screen);
    return 0;
  }

  // Variables for input handling
  int input_scroll_offset = 0; // For horizontal scrolling
  int ch;

  // Function to redraw input window
  void redraw_input() {
    werase(input_win);
    box(input_win, 0, 0);

    int visible_width = input_width - 4;
    int name_len = strlen(stash_name);

    // Clear the content area with spaces FIRST
    for (int x = 1; x <= visible_width; x++) {
      mvwaddch(input_win, 1, x, ' ');
    }

    // Calculate what part of the name to show
    int display_start = input_scroll_offset;
    int display_end = display_start + visible_width;
    if (display_end > name_len)
      display_end = name_len;

    // Show the visible portion of the name ON TOP of cleared spaces
    for (int i = display_start; i < display_end; i++) {
      mvwaddch(input_win, 1, 1 + (i - display_start), stash_name[i]);
    }

    // Header
    wattron(input_win, COLOR_PAIR(4));
    mvwprintw(input_win, 0, 2,
              " Enter stash name (ESC to cancel, Enter to confirm) ");
    wattroff(input_win, COLOR_PAIR(4));

    wrefresh(input_win);
  }

  // Initial draw
  redraw_input();

  // Position initial cursor
  int cursor_pos = strlen(stash_name) - input_scroll_offset;
  int visible_width = input_width - 4;
  if (cursor_pos > visible_width - 1)
    cursor_pos = visible_width - 1;
  if (cursor_pos < 0)
    cursor_pos = 0;
  wmove(input_win, 1, 1 + cursor_pos);
  wrefresh(input_win);

  curs_set(1);
  noecho();

  // Main input loop
  while (1) {
    ch = getch();

    if (ch == 27) {
      // ESC to cancel
      stash_name[0] = '\0'; // Clear name
      break;
    }

    if (ch == '\n' || ch == '\r') {
      // Enter to confirm - accept if name has content
      if (strlen(stash_name) > 0) {
        break;
      }
    } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      // Handle backspace
      int len = strlen(stash_name);
      if (len > 0) {
        stash_name[len - 1] = '\0';

        // Adjust scroll if needed
        int visible_width = input_width - 4;
        if (len - 1 <= input_scroll_offset) {
          input_scroll_offset = (len - 1) - (visible_width - 5);
          if (input_scroll_offset < 0)
            input_scroll_offset = 0;
        }
        redraw_input();
      }
    } else if (ch >= 32 && ch <= 126) {
      // Regular character input
      int len = strlen(stash_name);
      if (len < max_len - 1) {
        stash_name[len] = ch;
        stash_name[len + 1] = '\0';

        // Auto-scroll horizontally if needed
        int visible_width = input_width - 4;
        if (len + 1 > input_scroll_offset + visible_width - 5) {
          input_scroll_offset = (len + 1) - (visible_width - 5);
        }
        redraw_input();
      }
    }

    // Position cursor
    int cursor_pos = strlen(stash_name) - input_scroll_offset;
    int visible_width = input_width - 4;
    if (cursor_pos > visible_width - 1)
      cursor_pos = visible_width - 1;
    if (cursor_pos < 0)
      cursor_pos = 0;
    wmove(input_win, 1, 1 + cursor_pos);
    wrefresh(input_win);
  }

  // Restore settings
  curs_set(0); // Hide cursor

  // Clean up window
  delwin(input_win);

  // Restore the screen
  if (saved_screen) {
    overwrite(saved_screen, stdscr);
    delwin(saved_screen);
  }

  // Force a complete redraw
  clear();
  refresh();

  return strlen(stash_name) > 0 ? 1 : 0;
}

/**
 * Create a new git stash
 */
int create_ncurses_git_stash(NCursesDiffViewer *viewer) {
  if (!viewer)
    return 0;

  // Get stash name from user
  char stash_name[256] = "";
  if (!get_stash_name_input(stash_name, sizeof(stash_name))) {
    return 0; // User cancelled
  }

  int result = create_git_stash_with_name(stash_name);

  if (result) {
    // Refresh everything after creating stash
    get_ncurses_changed_files(viewer);
    get_ncurses_git_stashes(viewer);
    get_commit_history(viewer);

    // Reset file selection since changes are stashed
    viewer->selected_file = 0;
    viewer->file_line_count = 0;
    viewer->file_scroll_offset = 0;

    // Reload current file if any files still exist
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_file_with_staging_info(
          viewer, viewer->files[viewer->selected_file].filename);
    }
  }

  return result;
}

/**
 * Render the stash list window
 */
void render_stash_list_window(NCursesDiffViewer *viewer) {
  if (!viewer || !viewer->stash_list_win)
    return;

  // Clear the entire window first
  werase(viewer->stash_list_win);

  // Draw rounded border and title
  draw_rounded_box(viewer->stash_list_win);
  mvwprintw(viewer->stash_list_win, 0, 2, " 5. Stashes ");

  int max_stashes_visible = viewer->stash_panel_height - 2;

  // Clear the content area with spaces
  for (int y = 1; y < viewer->stash_panel_height - 1; y++) {
    for (int x = 1; x < viewer->file_panel_width - 1; x++) {
      mvwaddch(viewer->stash_list_win, y, x, ' ');
    }
  }

  if (viewer->stash_count == 0) {
    // Show "No stashes" message
    mvwprintw(viewer->stash_list_win, 1, 2, "No stashes available");
  } else {
    for (int i = 0; i < max_stashes_visible && i < viewer->stash_count; i++) {
      int y = i + 1;

      // Check if this stash line should be highlighted
      int is_selected_stash = (i == viewer->selected_stash &&
                               viewer->current_mode == NCURSES_MODE_STASH_LIST);
      
      // Check if this stash is currently being viewed
      int is_being_viewed = (i == viewer->selected_stash &&
                             viewer->current_mode == NCURSES_MODE_STASH_VIEW);

      // Apply line highlight if selected
      if (is_selected_stash) {
        wattron(viewer->stash_list_win, COLOR_PAIR(5));
      }

      // Show view indicator (currently being viewed)
      if (is_being_viewed) {
        wattron(viewer->stash_list_win, COLOR_PAIR(1)); // Green for active view
        mvwprintw(viewer->stash_list_win, y, 0, "*");
        wattroff(viewer->stash_list_win, COLOR_PAIR(1));
      } else {
        mvwprintw(viewer->stash_list_win, y, 0, " ");
      }

      // Show selection indicator
      if (is_selected_stash) {
        mvwprintw(viewer->stash_list_win, y, 1, ">");
      } else {
        mvwprintw(viewer->stash_list_win, y, 1, " ");
      }

      // Show stash info (truncated to fit panel)
      int max_stash_len = viewer->file_panel_width - 4;
      char truncated_stash[256];
      if ((int)strlen(viewer->stashes[i].stash_info) > max_stash_len) {
        strncpy(truncated_stash, viewer->stashes[i].stash_info,
                max_stash_len - 2);
        truncated_stash[max_stash_len - 2] = '\0';
        strcat(truncated_stash, "..");
      } else {
        strcpy(truncated_stash, viewer->stashes[i].stash_info);
      }

      // Show stash info with yellow time part and white message part
      if (is_selected_stash) {
        wattroff(viewer->stash_list_win, COLOR_PAIR(5));
      }

      // Find the colon after "On [branch]:"
      char *colon_pos = strstr(truncated_stash, ": ");
      if (colon_pos) {
        // Split at the colon
        int time_part_len = colon_pos - truncated_stash + 1; // Include the ":"

        // Find where " On " starts to insert space before it for single digits
        char *on_pos = strstr(truncated_stash, " On ");

        if (on_pos) {
          // Check if time is single digit (e.g., "6h" or "4m")
          int is_single_digit = 0;
          int time_len = on_pos - truncated_stash;
          if (time_len == 2) { // Single digit + letter (e.g., "6h", "4m")
            char first_char = truncated_stash[0];
            char second_char = truncated_stash[1];
            if (first_char >= '1' && first_char <= '9' &&
                (second_char == 'h' || second_char == 'm' ||
                 second_char == 'd' || second_char == 'w')) {
              is_single_digit = 1;
            }
          }

          // Print time part in yellow
          wattron(viewer->stash_list_win, COLOR_PAIR(4));
          mvwprintw(viewer->stash_list_win, y, 2, "%.*s", time_len,
                    truncated_stash);

          // Add extra space for single digit times
          if (is_single_digit) {
            mvwprintw(viewer->stash_list_win, y, 2 + time_len, " ");
            time_len++;
          }

          // Print " On [branch]:" part in yellow
          mvwprintw(viewer->stash_list_win, y, 2 + time_len, "%.*s",
                    (int)(colon_pos - on_pos + 1), on_pos);
          wattroff(viewer->stash_list_win, COLOR_PAIR(4));

          // Print message part in normal color (white)
          mvwprintw(viewer->stash_list_win, y,
                    2 + time_part_len + (is_single_digit ? 1 : 0), "%s",
                    colon_pos + 1);
        } else {
          // Fallback: print entire yellow part if no " On " found
          wattron(viewer->stash_list_win, COLOR_PAIR(4));
          mvwprintw(viewer->stash_list_win, y, 2, "%.*s", time_part_len,
                    truncated_stash);
          wattroff(viewer->stash_list_win, COLOR_PAIR(4));

          // Print message part in normal color (white)
          mvwprintw(viewer->stash_list_win, y, 2 + time_part_len, "%s",
                    colon_pos + 1);
        }
      } else {
        // Fallback: print entire string in yellow if no colon found
        wattron(viewer->stash_list_win, COLOR_PAIR(4));
        mvwprintw(viewer->stash_list_win, y, 2, "%s", truncated_stash);
        wattroff(viewer->stash_list_win, COLOR_PAIR(4));
      }

      if (is_selected_stash) {
        wattron(viewer->stash_list_win, COLOR_PAIR(5));
      }
    }
  }

  wrefresh(viewer->stash_list_win);
}

void render_branch_list_window(NCursesDiffViewer *viewer) {
  if (!viewer || !viewer->branch_list_win)
    return;

  werase(viewer->branch_list_win);

  draw_rounded_box(viewer->branch_list_win);
  mvwprintw(viewer->branch_list_win, 0, 2, " 3. Branches ");

  int max_branches_visible = viewer->branch_panel_height - 2;

  for (int y = 1; y < viewer->branch_panel_height - 1; y++) {
    for (int x = 1; x < viewer->file_panel_width - 1; x++) {
      mvwaddch(viewer->branch_list_win, y, x, ' ');
    }
  }

  if (viewer->branch_count == 0) {
    mvwprintw(viewer->branch_list_win, 1, 2, "No branches available");
  } else {
    for (int i = 0; i < max_branches_visible && i < viewer->branch_count; i++) {
      int y = i + 1;

      int is_selected_branch =
          (i == viewer->selected_branch &&
           viewer->current_mode == NCURSES_MODE_BRANCH_LIST);
      int is_current_branch =
          viewer->branches[i].status; // status = 1 for current branch

      if (is_selected_branch) {
        wattron(viewer->branch_list_win, COLOR_PAIR(5));
      }

      // Show selection arrow
      if (is_selected_branch) {
        mvwprintw(viewer->branch_list_win, y, 1, ">");
      } else {
        mvwprintw(viewer->branch_list_win, y, 1, " ");
      }

      // Prepare branch display with asterisk for current branch and status
      int max_branch_len = viewer->file_panel_width -
                           15; // Leave more space for status indicators
      char display_branch[300];
      char status_indicator[50] = "";

      // Create status indicator
      if (viewer->branches[i].commits_ahead > 0 &&
          viewer->branches[i].commits_behind > 0) {
        snprintf(status_indicator, sizeof(status_indicator), " %d↑%d↓", 
                 viewer->branches[i].commits_ahead,
                 viewer->branches[i].commits_behind);
      } else if (viewer->branches[i].commits_ahead > 0) {
        snprintf(status_indicator, sizeof(status_indicator), " %d↑", 
                 viewer->branches[i].commits_ahead);
      } else if (viewer->branches[i].commits_behind > 0) {
        snprintf(status_indicator, sizeof(status_indicator), " %d↓", 
                 viewer->branches[i].commits_behind);
      }

      if (is_current_branch) {
        snprintf(display_branch, sizeof(display_branch), "* %s",
                 viewer->branches[i].name);
      } else {
        snprintf(display_branch, sizeof(display_branch), "  %s",
                 viewer->branches[i].name);
      }

      // Truncate branch name if too long
      if ((int)strlen(display_branch) > max_branch_len) {
        display_branch[max_branch_len - 2] = '.';
        display_branch[max_branch_len - 1] = '.';
        display_branch[max_branch_len] = '\0';
      }

      // Color the branch name
      if (is_current_branch) {
        wattron(viewer->branch_list_win,
                COLOR_PAIR(1)); // Green for current branch
      } else {
        wattron(viewer->branch_list_win,
                COLOR_PAIR(4)); // Yellow for other branches
      }

      mvwprintw(viewer->branch_list_win, y, 2, "%s", display_branch);

      if (is_current_branch) {
        wattroff(viewer->branch_list_win, COLOR_PAIR(1));
      } else {
        wattroff(viewer->branch_list_win, COLOR_PAIR(4));
      }

      // Add status indicator with appropriate colors
      if (strlen(status_indicator) > 0) {
        if (is_selected_branch) {
          wattroff(viewer->branch_list_win, COLOR_PAIR(5));
        }

        if (viewer->branches[i].commits_behind > 0) {
          wattron(viewer->branch_list_win, COLOR_PAIR(2)); // Red for behind
        } else {
          wattron(viewer->branch_list_win, COLOR_PAIR(1)); // Green for ahead
        }

        mvwprintw(viewer->branch_list_win, y, 2 + strlen(display_branch), "%s",
                  status_indicator);

        if (viewer->branches[i].commits_behind > 0) {
          wattroff(viewer->branch_list_win, COLOR_PAIR(2));
        } else {
          wattroff(viewer->branch_list_win, COLOR_PAIR(1));
        }

        if (is_selected_branch) {
          wattron(viewer->branch_list_win, COLOR_PAIR(5));
        }
      }

      // Add push/pull status indicators next to the branch
      char branch_sync_text[32] = "";
      char *spinner_chars[] = {"|", "/", "-", "\\"};
      int spinner_idx = (viewer->branch_animation_frame / 1) % 4;

      // Check if this branch is being pushed
      if (i == viewer->pushing_branch_index) {
        if (viewer->branch_push_status >= SYNC_STATUS_PUSHING_APPEARING &&
            viewer->branch_push_status <= SYNC_STATUS_PUSHING_DISAPPEARING) {
          if (viewer->branch_push_status == SYNC_STATUS_PUSHING_VISIBLE) {
            snprintf(branch_sync_text, sizeof(branch_sync_text), " Pushing %s",
                     spinner_chars[spinner_idx]);
          } else {
            char partial_text[16] = "";
            int chars_to_show = viewer->branch_text_char_count;
            if (chars_to_show > 7)
              chars_to_show = 7;
            if (chars_to_show > 0) {
              strncpy(partial_text, "Pushing", chars_to_show);
              partial_text[chars_to_show] = '\0';
              snprintf(branch_sync_text, sizeof(branch_sync_text), " %s",
                       partial_text);
            }
          }
        } else if (viewer->branch_push_status >= SYNC_STATUS_PUSHED_APPEARING &&
                   viewer->branch_push_status <=
                       SYNC_STATUS_PUSHED_DISAPPEARING) {
          char partial_text[16] = "";
          int chars_to_show = viewer->branch_text_char_count;
          if (chars_to_show > 7)
            chars_to_show = 7;
          if (chars_to_show > 0) {
            strncpy(partial_text, "Pushed!", chars_to_show);
            partial_text[chars_to_show] = '\0';
            snprintf(branch_sync_text, sizeof(branch_sync_text), " %s",
                     partial_text);
          }
        }
      }

      // Check if this branch is being pulled
      if (i == viewer->pulling_branch_index) {
        if (viewer->branch_pull_status >= SYNC_STATUS_PULLING_APPEARING &&
            viewer->branch_pull_status <= SYNC_STATUS_PULLING_DISAPPEARING) {
          if (viewer->branch_pull_status == SYNC_STATUS_PULLING_VISIBLE) {
            snprintf(branch_sync_text, sizeof(branch_sync_text), " Pulling %s",
                     spinner_chars[spinner_idx]);
          } else {
            char partial_text[16] = "";
            int chars_to_show = viewer->branch_text_char_count;
            if (chars_to_show > 7)
              chars_to_show = 7;
            if (chars_to_show > 0) {
              strncpy(partial_text, "Pulling", chars_to_show);
              partial_text[chars_to_show] = '\0';
              snprintf(branch_sync_text, sizeof(branch_sync_text), " %s",
                       partial_text);
            }
          }
        } else if (viewer->branch_pull_status >= SYNC_STATUS_PULLED_APPEARING &&
                   viewer->branch_pull_status <=
                       SYNC_STATUS_PULLED_DISAPPEARING) {
          char partial_text[16] = "";
          int chars_to_show = viewer->branch_text_char_count;
          if (chars_to_show > 7)
            chars_to_show = 7;
          if (chars_to_show > 0) {
            strncpy(partial_text, "Pulled!", chars_to_show);
            partial_text[chars_to_show] = '\0';
            snprintf(branch_sync_text, sizeof(branch_sync_text), " %s",
                     partial_text);
          }
        }
      }

      // Display the branch sync status
      if (strlen(branch_sync_text) > 0) {
        if (is_selected_branch) {
          wattroff(viewer->branch_list_win, COLOR_PAIR(5));
        }

        wattron(viewer->branch_list_win,
                COLOR_PAIR(4)); // Yellow for sync status
        mvwprintw(viewer->branch_list_win, y,
                  2 + strlen(display_branch) + strlen(status_indicator), "%s",
                  branch_sync_text);
        wattroff(viewer->branch_list_win, COLOR_PAIR(4));

        if (is_selected_branch) {
          wattron(viewer->branch_list_win, COLOR_PAIR(5));
        }
      }

      if (is_selected_branch) {
        wattroff(viewer->branch_list_win, COLOR_PAIR(5));
      }
    }
  }
  wrefresh(viewer->branch_list_win);
}

/**
 * Parse and load content lines from text with diff highlighting
 */
int parse_content_lines(NCursesDiffViewer *viewer, const char *content) {
  if (!viewer || !content) {
    return 0;
  }

  viewer->file_line_count = 0;
  viewer->file_scroll_offset = 0;
  viewer->file_cursor_line = 0;

  // Parse line by line, preserving empty lines
  const char *line_start = content;
  const char *line_end;

  while (*line_start && viewer->file_line_count < MAX_FULL_FILE_LINES) {
    NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];

    // Find end of current line
    line_end = strchr(line_start, '\n');
    if (!line_end) {
      line_end = line_start + strlen(line_start);
    }

    // Calculate line length
    size_t line_len = line_end - line_start;

    // Copy line content (truncate if too long)
    size_t copy_len = (line_len < sizeof(file_line->line) - 1)
                          ? line_len
                          : sizeof(file_line->line) - 1;
    strncpy(file_line->line, line_start, copy_len);
    file_line->line[copy_len] = '\0';

    // Determine line type for syntax highlighting
    if (line_len == 0) {
      file_line->type = ' '; // Empty line
    } else if (strncmp(file_line->line, "diff --git", 10) == 0 ||
               strncmp(file_line->line, "index ", 6) == 0 ||
               strncmp(file_line->line, "--- ", 4) == 0 ||
               strncmp(file_line->line, "+++ ", 4) == 0) {
      file_line->type = '@'; // Use @ for headers
    } else if (line_len > 1 && file_line->line[0] == '@' &&
               file_line->line[1] == '@') {
      file_line->type = '@'; // Hunk headers
    } else if (line_len > 0 && file_line->line[0] == '+') {
      file_line->type = '+'; // Added lines
    } else if (line_len > 0 && file_line->line[0] == '-') {
      file_line->type = '-'; // Removed lines
    } else if (strstr(file_line->line, " | ") &&
               (strstr(file_line->line, "+") || strstr(file_line->line, "-") ||
                strstr(file_line->line, "Bin"))) {
      file_line->type = 's'; // File statistics lines (special type)
    } else if (strstr(file_line->line, " files changed") ||
               strstr(file_line->line, " insertions") ||
               strstr(file_line->line, " deletions")) {
      file_line->type = 's'; // Summary statistics lines
    } else if (strncmp(file_line->line, "commit ", 7) == 0) {
      file_line->type = 'h'; // Commit header
    } else if (strncmp(file_line->line, "Author: ", 8) == 0 ||
               strncmp(file_line->line, "Date: ", 6) == 0) {
      file_line->type = 'i'; // Commit info lines
    } else {
      file_line->type = ' '; // Normal/context lines
    }

    file_line->is_diff_line = (file_line->type != ' ') ? 1 : 0;

    viewer->file_line_count++;

    // Move to next line
    if (*line_end == '\n') {
      line_start = line_end + 1;
    } else {
      break; // End of content
    }
  }
  return viewer->file_line_count;
}

/**
 * Load commit details for viewing
 */
int load_commit_for_viewing(NCursesDiffViewer *viewer,
                            const char *commit_hash) {
  if (!viewer || !commit_hash) {
    return 0;
  }

  char *commit_content = malloc(50000); // Large buffer for commit details
  if (!commit_content) {
    return 0;
  }

  if (get_commit_details(commit_hash, commit_content, 50000)) {
    int result = parse_content_lines(viewer, commit_content);
    free(commit_content);
    return result;
  }

  free(commit_content);
  return 0;
}

/**
 * Load stash details for viewing
 */
int load_stash_for_viewing(NCursesDiffViewer *viewer, int stash_index) {
  if (!viewer || stash_index < 0) {
    return 0;
  }

  char *stash_content = malloc(50000); // Large buffer for stash details
  if (!stash_content) {
    return 0;
  }

  if (get_stash_diff(stash_index, stash_content, 50000)) {
    int result = parse_content_lines(viewer, stash_content);
    free(stash_content);
    return result;
  }

  free(stash_content);
  return 0;
}

/**
 * Load commits for a specific branch for the hover preview
 */
int load_branch_commits(NCursesDiffViewer *viewer, const char *branch_name) {
  if (!viewer || !branch_name) {
    return 0;
  }

  // Only load if it's a different branch than currently loaded
  if (strcmp(viewer->current_branch_for_commits, branch_name) == 0) {
    return viewer->branch_commit_count; // Already loaded
  }

  viewer->branch_commit_count =
      get_branch_commits(branch_name, viewer->branch_commits, MAX_COMMITS);

  strncpy(viewer->current_branch_for_commits, branch_name,
          sizeof(viewer->current_branch_for_commits) - 1);
  viewer
      ->current_branch_for_commits[sizeof(viewer->current_branch_for_commits) -
                                   1] = '\0';

  return viewer->branch_commit_count;
}

/**
 * Parse branch commits into navigable lines for branch view mode
 */
int parse_branch_commits_to_lines(NCursesDiffViewer *viewer) {
  if (!viewer || viewer->branch_commit_count == 0) {
    return 0;
  }

  viewer->file_line_count = 0;
  viewer->file_scroll_offset = 0;
  viewer->file_cursor_line = 0;

  // Parse each commit into file_lines for navigation
  for (int commit_idx = 0; commit_idx < viewer->branch_commit_count &&
                           viewer->file_line_count < MAX_FULL_FILE_LINES;
       commit_idx++) {

    char *commit_text = viewer->branch_commits[commit_idx];
    char *line_start = commit_text;
    char *line_end;

    // Parse each line of the commit
    while ((line_end = strchr(line_start, '\n')) != NULL &&
           viewer->file_line_count < MAX_FULL_FILE_LINES) {
      *line_end = '\0'; // Temporarily null-terminate

      NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
      strncpy(file_line->line, line_start, sizeof(file_line->line) - 1);
      file_line->line[sizeof(file_line->line) - 1] = '\0';

      // Set line type for appropriate coloring
      if (strncmp(line_start, "commit ", 7) == 0) {
        file_line->type = 'h'; // Commit header
      } else if (strncmp(line_start, "Author:", 7) == 0 ||
                 strncmp(line_start, "Date:", 5) == 0) {
        file_line->type = 'i'; // Info line
      } else {
        file_line->type = ' '; // Regular line
      }

      file_line->is_diff_line = 0;
      viewer->file_line_count++;

      *line_end = '\n'; // Restore newline
      line_start = line_end + 1;
    }

    // Handle last line if no newline at end
    if (strlen(line_start) > 0 &&
        viewer->file_line_count < MAX_FULL_FILE_LINES) {
      NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
      strncpy(file_line->line, line_start, sizeof(file_line->line) - 1);
      file_line->line[sizeof(file_line->line) - 1] = '\0';
      file_line->type = ' ';
      file_line->is_diff_line = 0;
      viewer->file_line_count++;
    }

    // Add spacing between commits
    if (viewer->file_line_count < MAX_FULL_FILE_LINES) {
      NCursesFileLine *empty_line =
          &viewer->file_lines[viewer->file_line_count];
      strcpy(empty_line->line, "");
      empty_line->type = ' ';
      empty_line->is_diff_line = 0;
      viewer->file_line_count++;
    }
  }

  return viewer->file_line_count;
}

/**
 * Start background fetch process
 */
void start_background_fetch(NCursesDiffViewer *viewer) {
  if (!viewer || viewer->fetch_in_progress ||
      viewer->critical_operation_in_progress) {
    return;
  }

  viewer->fetch_pid = fork();
  if (viewer->fetch_pid == 0) {
    // Child process: do the fetch
    system("git fetch --all --quiet >/dev/null 2>&1");
    exit(0);
  } else if (viewer->fetch_pid > 0) {
    // Parent process: mark fetch as in progress
    viewer->fetch_in_progress = 1;
    viewer->sync_status = SYNC_STATUS_SYNCING_APPEARING;
    viewer->animation_frame = 0;
    viewer->text_char_count = 0;
  }
}

/**
 * Check if background fetch is complete and update UI accordingly
 */
void check_background_fetch(NCursesDiffViewer *viewer) {
  if (!viewer || !viewer->fetch_in_progress) {
    return;
  }

  int status;
  pid_t result = waitpid(viewer->fetch_pid, &status, WNOHANG);

  if (result == viewer->fetch_pid) {
    // Fetch completed
    viewer->fetch_in_progress = 0;
    viewer->fetch_pid = -1;

    // Preserve current positions during refresh
    int preserved_file_scroll = viewer->file_scroll_offset;
    int preserved_file_cursor = viewer->file_cursor_line;
    int preserved_selected_file = viewer->selected_file;

    // Update only the necessary data without blocking UI
    get_ncurses_changed_files(viewer);
    get_commit_history(viewer);
    get_ncurses_git_branches(viewer);

    // Restore file selection if still valid
    if (preserved_selected_file < viewer->file_count) {
      viewer->selected_file = preserved_selected_file;

      // Reload current file if in file mode and restore position
      if ((viewer->current_mode == NCURSES_MODE_FILE_LIST ||
           viewer->current_mode == NCURSES_MODE_FILE_VIEW) &&
          viewer->file_count > 0) {
        load_file_with_staging_info(
            viewer, viewer->files[viewer->selected_file].filename);

        // Restore scroll position if still valid
        if (preserved_file_cursor < viewer->file_line_count) {
          viewer->file_cursor_line = preserved_file_cursor;
        }
        if (preserved_file_scroll < viewer->file_line_count) {
          viewer->file_scroll_offset = preserved_file_scroll;
        }
      }
    }

    // If we're in branch mode and have commits loaded, refresh them
    if (viewer->current_mode == NCURSES_MODE_BRANCH_LIST ||
        viewer->current_mode == NCURSES_MODE_BRANCH_VIEW) {
      if (viewer->branch_count > 0 &&
          strlen(viewer->current_branch_for_commits) > 0) {
        load_branch_commits(viewer, viewer->current_branch_for_commits);
        if (viewer->current_mode == NCURSES_MODE_BRANCH_VIEW) {
          // Preserve cursor position in branch view too
          int prev_cursor = viewer->file_cursor_line;
          int prev_scroll = viewer->file_scroll_offset;
          parse_branch_commits_to_lines(viewer);
          if (prev_cursor < viewer->file_line_count) {
            viewer->file_cursor_line = prev_cursor;
          }
          if (prev_scroll < viewer->file_line_count) {
            viewer->file_scroll_offset = prev_scroll;
          }
        }
      }
    }

    // Show completion status briefly
    viewer->sync_status = SYNC_STATUS_SYNCED_APPEARING;
    viewer->animation_frame = 0;
    viewer->text_char_count = 0;
  } else if (result == -1) {
    // Error occurred
    viewer->fetch_in_progress = 0;
    viewer->fetch_pid = -1;
    viewer->sync_status = SYNC_STATUS_IDLE;
  }
}

/**
 * Update preview based on current mode and selection
 */
void update_preview_for_current_selection(NCursesDiffViewer *viewer) {
  if (!viewer) return;
  
  static NCursesViewMode last_mode = (NCursesViewMode)-1;
  static int last_file = -1;
  static int last_commit = -1;
  static int last_branch = -1;
  static int last_stash = -1;
  
  int needs_update = 0;
  
  if (viewer->current_mode == NCURSES_MODE_FILE_LIST) {
    if (last_mode != viewer->current_mode || last_file != viewer->selected_file) {
      if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
        // Load diff preview instead of raw file content
        load_file_with_staging_info(viewer, viewer->files[viewer->selected_file].filename);
      }
      last_file = viewer->selected_file;
      needs_update = 1;
    }
  } else if (viewer->current_mode == NCURSES_MODE_COMMIT_LIST) {
    if (last_mode != viewer->current_mode || last_commit != viewer->selected_commit) {
      if (viewer->commit_count > 0 && viewer->selected_commit < viewer->commit_count) {
        load_commit_for_viewing(viewer, viewer->commits[viewer->selected_commit].hash);
      }
      last_commit = viewer->selected_commit;
      needs_update = 1;
    }
  } else if (viewer->current_mode == NCURSES_MODE_BRANCH_LIST) {
    if (last_mode != viewer->current_mode || last_branch != viewer->selected_branch) {
      if (viewer->branch_count > 0 && viewer->selected_branch < viewer->branch_count) {
        load_branch_commits(viewer, viewer->branches[viewer->selected_branch].name);
        parse_branch_commits_to_lines(viewer); // Convert to file_lines for preview display
      }
      last_branch = viewer->selected_branch;
      needs_update = 1;
    }
  } else if (viewer->current_mode == NCURSES_MODE_STASH_LIST) {
    if (last_mode != viewer->current_mode || last_stash != viewer->selected_stash) {
      if (viewer->stash_count > 0 && viewer->selected_stash < viewer->stash_count) {
        load_stash_for_viewing(viewer, viewer->selected_stash);
      }
      last_stash = viewer->selected_stash;
      needs_update = 1;
    }
  }
  
  if (needs_update) {
    last_mode = viewer->current_mode;
  }
}

/**
 * Load file preview showing the top of the file content
 */
int load_file_preview(NCursesDiffViewer *viewer, const char *filename) {
  if (!viewer || !filename)
    return 0;
    
  viewer->file_line_count = 0;
  viewer->file_scroll_offset = 0;
  viewer->file_cursor_line = 0;
  
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    return 0;
  }
  
  char line[1024];
  int line_count = 0;
  
  // Load first 50 lines of the file for preview
  while (fgets(line, sizeof(line), fp) && line_count < 50 &&
         line_count < MAX_FULL_FILE_LINES) {
    // Remove trailing newline
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
    }
    
    // Store the line as a context line (no diff markings)
    strncpy(viewer->file_lines[line_count].line, line, 
            sizeof(viewer->file_lines[line_count].line) - 1);
    viewer->file_lines[line_count].line[sizeof(viewer->file_lines[line_count].line) - 1] = '\0';
    viewer->file_lines[line_count].type = ' '; // Context line
    viewer->file_lines[line_count].is_diff_line = 0;
    viewer->file_lines[line_count].is_staged = 0;
    viewer->file_lines[line_count].hunk_id = 0;
    viewer->file_lines[line_count].line_number_old = line_count + 1;
    viewer->file_lines[line_count].line_number_new = line_count + 1;
    viewer->file_lines[line_count].is_context = 1;
    
    line_count++;
  }
  
  fclose(fp);
  viewer->file_line_count = line_count;
  
  // Reset staged content since this is just a preview
  viewer->staged_line_count = 0;
  viewer->staged_cursor_line = 0;
  viewer->staged_scroll_offset = 0;
  
  return 1;
}

/**
 * Helper function to wrap a long line into multiple display lines
 * Returns the number of wrapped lines created
 */
int wrap_line_to_width(const char *input_line, char wrapped_lines[][1024], int max_lines, int width) {
  int input_len = strlen(input_line);
  int line_count = 0;
  int input_pos = 0;
  
  // If line fits in width, just copy it
  if (input_len <= width) {
    strcpy(wrapped_lines[0], input_line);
    return 1;
  }
  
  while (input_pos < input_len && line_count < max_lines - 1) {
    int chars_to_copy = width;
    
    // Don't exceed remaining input length
    if (input_pos + chars_to_copy > input_len) {
      chars_to_copy = input_len - input_pos;
    }
    
    // Copy the segment
    strncpy(wrapped_lines[line_count], input_line + input_pos, chars_to_copy);
    wrapped_lines[line_count][chars_to_copy] = '\0';
    
    input_pos += chars_to_copy;
    line_count++;
  }
  
  return line_count;
}

/**
 * Render a single line with proper wrapping and coloring
 * Returns the number of display rows used
 */
int render_wrapped_line(WINDOW *win, const char *line, int start_y, int start_x, 
                       int width, int max_rows, int color_pair, int reverse) {
  char wrapped_lines[10][1024]; // Support up to 10 wrapped lines per original line
  int wrap_count = wrap_line_to_width(line, wrapped_lines, 10, width - start_x);
  
  int rows_used = 0;
  for (int i = 0; i < wrap_count && rows_used < max_rows; i++) {
    if (reverse) {
      wattron(win, A_REVERSE);
    }
    if (color_pair > 0) {
      wattron(win, COLOR_PAIR(color_pair));
    }
    
    mvwprintw(win, start_y + rows_used, start_x, "%s", wrapped_lines[i]);
    
    if (color_pair > 0) {
      wattroff(win, COLOR_PAIR(color_pair));
    }
    if (reverse) {
      wattroff(win, A_REVERSE);
    }
    
    rows_used++;
  }
  
  return rows_used;
}

/**
 * Calculate how many display lines a wrapped line will take
 */
int calculate_wrapped_line_height(const char *line, int width) {
  int line_len = strlen(line);
  if (line_len <= width) {
    return 1;
  }
  return (line_len + width - 1) / width; // Ceiling division
}

/**
 * Move cursor up/down while skipping empty lines
 * direction: -1 for up, 1 for down
 */
void move_cursor_smart(NCursesDiffViewer *viewer, int direction) {
  if (!viewer || viewer->file_line_count == 0) {
    return;
  }

  int original_cursor = viewer->file_cursor_line;
  int new_cursor = viewer->file_cursor_line;
  int attempts = 0;
  const int max_attempts = viewer->file_line_count; // Prevent infinite loops

  do {
    new_cursor += direction;
    attempts++;

    // Bounds checking
    if (new_cursor < 0) {
      new_cursor = 0;
      break;
    }
    if (new_cursor >= viewer->file_line_count) {
      new_cursor = viewer->file_line_count - 1;
      break;
    }

    // Check if current line is empty or just whitespace
    NCursesFileLine *line = &viewer->file_lines[new_cursor];
    char *trimmed = line->line;

    // Skip leading whitespace
    while (*trimmed == ' ' || *trimmed == '\t') {
      trimmed++;
    }

    // If line has content after trimming, or we've tried too many times, stop
    // here
    if (*trimmed != '\0' || attempts >= max_attempts) {
      break;
    }

  } while (attempts < max_attempts);

  int cursor_movement = new_cursor - original_cursor;

  viewer->file_cursor_line = new_cursor;

  int height, width;
  getmaxyx(viewer->file_content_win, height, width);
  int max_lines_visible = height - 2;

  if (direction == -1) {
    int cursor_display_pos =
        viewer->file_cursor_line - viewer->file_scroll_offset;

    if (cursor_display_pos < 3) {
      int scroll_adjustment = 3 - cursor_display_pos;
      viewer->file_scroll_offset -= scroll_adjustment;

      if (viewer->file_scroll_offset < 0) {
        viewer->file_scroll_offset = 0;
      }
    }
  } else {
    int cursor_display_pos =
        viewer->file_cursor_line - viewer->file_scroll_offset;

    if (cursor_display_pos >= max_lines_visible - 3) {
      int scroll_adjustment = cursor_display_pos - (max_lines_visible - 4);
      viewer->file_scroll_offset += scroll_adjustment;

      int max_scroll = viewer->file_line_count - max_lines_visible;
      if (max_scroll < 0)
        max_scroll = 0;
      if (viewer->file_scroll_offset > max_scroll) {
        viewer->file_scroll_offset = max_scroll;
      }
    }
  }
}

/**
 * Move cursor up/down in unstaged pane with proper scrolling
 * direction: -1 for up, 1 for down
 */
void move_cursor_smart_unstaged(NCursesDiffViewer *viewer, int direction) {
  if (!viewer || viewer->file_line_count == 0) {
    return;
  }

  int original_cursor = viewer->file_cursor_line;
  int new_cursor = viewer->file_cursor_line;
  int attempts = 0;
  const int max_attempts = viewer->file_line_count;

  do {
    new_cursor += direction;
    attempts++;

    if (new_cursor < 0) {
      new_cursor = 0;
      break;
    }
    if (new_cursor >= viewer->file_line_count) {
      new_cursor = viewer->file_line_count - 1;
      break;
    }

    NCursesFileLine *line = &viewer->file_lines[new_cursor];
    char *trimmed = line->line;

    while (*trimmed == ' ' || *trimmed == '\t') {
      trimmed++;
    }

    if (*trimmed != '\0' || attempts >= max_attempts) {
      break;
    }

  } while (attempts < max_attempts);

  viewer->file_cursor_line = new_cursor;

  int height, width;
  getmaxyx(viewer->file_content_win, height, width);
  int split_line = height / 2;
  int unstaged_height = split_line - 1;
  
  // Calculate display positions accounting for wrapped lines
  int cursor_display_rows = 0;
  int scroll_display_rows = 0;
  
  // Count display rows from scroll offset to cursor
  for (int i = viewer->file_scroll_offset; i <= viewer->file_cursor_line && i < viewer->file_line_count; i++) {
    int line_height = calculate_wrapped_line_height(viewer->file_lines[i].line, width - 4);
    if (i < viewer->file_cursor_line) {
      cursor_display_rows += line_height;
    }
    if (i >= viewer->file_scroll_offset) {
      scroll_display_rows += line_height;
    }
  }
  
  if (direction == -1) {
    // Moving up - ensure cursor stays visible at top
    if (cursor_display_rows < 2) {
      // Need to scroll up to make cursor visible
      int target_rows = 2;
      int new_scroll_offset = viewer->file_cursor_line;
      int accumulated_rows = calculate_wrapped_line_height(viewer->file_lines[viewer->file_cursor_line].line, width - 4);
      
      while (new_scroll_offset > 0 && accumulated_rows < target_rows) {
        new_scroll_offset--;
        accumulated_rows += calculate_wrapped_line_height(viewer->file_lines[new_scroll_offset].line, width - 4);
      }
      
      viewer->file_scroll_offset = new_scroll_offset;
      if (viewer->file_scroll_offset < 0) {
        viewer->file_scroll_offset = 0;
      }
    }
  } else {
    // Moving down - ensure cursor stays visible at bottom
    if (cursor_display_rows >= unstaged_height - 2) {
      // Need to scroll down to make cursor visible
      int target_remaining_rows = unstaged_height - 3;
      int new_scroll_offset = viewer->file_cursor_line;
      int accumulated_rows = calculate_wrapped_line_height(viewer->file_lines[viewer->file_cursor_line].line, width - 4);
      
      while (new_scroll_offset > viewer->file_scroll_offset && accumulated_rows > target_remaining_rows) {
        new_scroll_offset--;
        accumulated_rows -= calculate_wrapped_line_height(viewer->file_lines[new_scroll_offset].line, width - 4);
      }
      
      if (new_scroll_offset > viewer->file_scroll_offset) {
        viewer->file_scroll_offset = new_scroll_offset;
      }
      
      int max_scroll = viewer->file_line_count - 1;
      if (viewer->file_scroll_offset > max_scroll) {
        viewer->file_scroll_offset = max_scroll;
      }
    }
  }
}

/**
 * Move cursor up/down in staged pane with proper scrolling
 * direction: -1 for up, 1 for down
 */
void move_cursor_smart_staged(NCursesDiffViewer *viewer, int direction) {
  if (!viewer || viewer->staged_line_count == 0) {
    return;
  }

  int height, width;
  getmaxyx(viewer->file_content_win, height, width);
  int split_line = height / 2;
  int staged_height = height - split_line - 2;
  
  if (direction == -1) {
    if (viewer->staged_cursor_line > 0) {
      viewer->staged_cursor_line--;
      
      // Calculate display positions accounting for wrapped lines
      int cursor_display_rows = 0;
      
      // Count display rows from scroll offset to cursor
      for (int i = viewer->staged_scroll_offset; i < viewer->staged_cursor_line && i < viewer->staged_line_count; i++) {
        cursor_display_rows += calculate_wrapped_line_height(viewer->staged_lines[i].line, width - 4);
      }
      
      if (cursor_display_rows < 1) {
        // Need to scroll up to make cursor visible
        int target_rows = 1;
        int new_scroll_offset = viewer->staged_cursor_line;
        int accumulated_rows = calculate_wrapped_line_height(viewer->staged_lines[viewer->staged_cursor_line].line, width - 4);
        
        while (new_scroll_offset > 0 && accumulated_rows < target_rows) {
          new_scroll_offset--;
          accumulated_rows += calculate_wrapped_line_height(viewer->staged_lines[new_scroll_offset].line, width - 4);
        }
        
        viewer->staged_scroll_offset = new_scroll_offset;
        if (viewer->staged_scroll_offset < 0) {
          viewer->staged_scroll_offset = 0;
        }
      }
    }
  } else {
    if (viewer->staged_cursor_line < viewer->staged_line_count - 1) {
      viewer->staged_cursor_line++;
      
      // Calculate display positions accounting for wrapped lines
      int cursor_display_rows = 0;
      
      // Count display rows from scroll offset to cursor
      for (int i = viewer->staged_scroll_offset; i < viewer->staged_cursor_line && i < viewer->staged_line_count; i++) {
        cursor_display_rows += calculate_wrapped_line_height(viewer->staged_lines[i].line, width - 4);
      }
      
      if (cursor_display_rows >= staged_height - 1) {
        // Need to scroll down to make cursor visible
        int target_remaining_rows = staged_height - 2;
        int new_scroll_offset = viewer->staged_cursor_line;
        int accumulated_rows = calculate_wrapped_line_height(viewer->staged_lines[viewer->staged_cursor_line].line, width - 4);
        
        while (new_scroll_offset > viewer->staged_scroll_offset && accumulated_rows > target_remaining_rows) {
          new_scroll_offset--;
          accumulated_rows -= calculate_wrapped_line_height(viewer->staged_lines[new_scroll_offset].line, width - 4);
        }
        
        if (new_scroll_offset > viewer->staged_scroll_offset) {
          viewer->staged_scroll_offset = new_scroll_offset;
        }
        
        int max_scroll = viewer->staged_line_count - 1;
        if (viewer->staged_scroll_offset > max_scroll) {
          viewer->staged_scroll_offset = max_scroll;
        }
      }
    }
  }
}

/**
 * Clean up ncurses diff viewer resources
 */
void cleanup_ncurses_diff_viewer(NCursesDiffViewer *viewer) {
  if (viewer) {
    // Clean up any background fetch process
    if (viewer->fetch_in_progress && viewer->fetch_pid > 0) {
      kill(viewer->fetch_pid, SIGTERM);
      waitpid(viewer->fetch_pid, NULL, 0);
    }

    if (viewer->file_list_win) {
      delwin(viewer->file_list_win);
    }
    if (viewer->file_content_win) {
      delwin(viewer->file_content_win);
    }
    if (viewer->commit_list_win) {
      delwin(viewer->commit_list_win);
    }
    if (viewer->stash_list_win) {
      delwin(viewer->stash_list_win);
    }
    if (viewer->branch_list_win) {
      delwin(viewer->branch_list_win);
    }
    if (viewer->status_bar_win) {
      delwin(viewer->status_bar_win);
    }
    
    // Clean up fuzzy search windows
    cleanup_fuzzy_search(viewer);
    
    // Clean up grep search windows
    cleanup_grep_search(viewer);
  }
  endwin();
}

/**
 * Initialize fuzzy search state
 */
void init_fuzzy_search(NCursesDiffViewer *viewer) {
  if (!viewer) return;
  
  viewer->fuzzy_search_active = 0;
  viewer->fuzzy_search_query[0] = '\0';
  viewer->fuzzy_search_query_len = 0;
  viewer->fuzzy_filtered_count = 0;
  viewer->fuzzy_selected_index = 0;
  viewer->fuzzy_scroll_offset = 0;
  viewer->fuzzy_input_win = NULL;
  viewer->fuzzy_list_win = NULL;
  
  // Initialize state tracking
  viewer->fuzzy_needs_full_redraw = 0;
  viewer->fuzzy_needs_input_redraw = 0;
  viewer->fuzzy_needs_list_redraw = 0;
  viewer->fuzzy_last_query[0] = '\0';
  viewer->fuzzy_last_selected = -1;
  viewer->fuzzy_last_scroll = -1;
  viewer->fuzzy_last_filtered_count = -1;
}

/**
 * Cleanup fuzzy search windows
 */
void cleanup_fuzzy_search(NCursesDiffViewer *viewer) {
  if (!viewer) return;
  
  if (viewer->fuzzy_input_win) {
    delwin(viewer->fuzzy_input_win);
    viewer->fuzzy_input_win = NULL;
  }
  if (viewer->fuzzy_list_win) {
    delwin(viewer->fuzzy_list_win);
    viewer->fuzzy_list_win = NULL;
  }
}

/**
 * Calculate fuzzy match score - higher score is better match
 * Returns 0 if no match, positive score if match
 */
int calculate_fuzzy_score(const char *pattern, const char *filename) {
  if (!pattern || !filename) return 0;
  if (strlen(pattern) == 0) return 1000; // Empty pattern matches everything with high score
  
  int pattern_len = strlen(pattern);
  int filename_len = strlen(filename);
  
  if (pattern_len > filename_len) return 0; // Pattern longer than filename
  
  int score = 0;
  int pattern_pos = 0;
  int consecutive_matches = 0;
  int first_char_bonus = 0;
  
  for (int i = 0; i < filename_len && pattern_pos < pattern_len; i++) {
    char p_char = tolower(pattern[pattern_pos]);
    char f_char = tolower(filename[i]);
    
    if (p_char == f_char) {
      // Base score for character match
      score += 1;
      
      // Bonus for consecutive matches (prioritizes contiguous substrings)
      consecutive_matches++;
      score += consecutive_matches * 5;
      
      // Bonus for matching at word boundaries
      if (i == 0 || filename[i-1] == '/' || filename[i-1] == '_' || filename[i-1] == '-' || filename[i-1] == '.') {
        score += 15;
      }
      
      // Bonus for matching first character
      if (pattern_pos == 0) {
        if (i == 0) {
          first_char_bonus = 50; // Very high bonus for first char match
        } else {
          // Find start of filename (after last /)
          const char *basename = strrchr(filename, '/');
          basename = basename ? basename + 1 : filename;
          if (filename + i == basename) {
            first_char_bonus = 30; // High bonus for basename first char
          }
        }
      }
      
      // Bonus for exact position match (same position in pattern and filename)
      if (i == pattern_pos) {
        score += 10;
      }
      
      pattern_pos++;
    } else {
      // Reset consecutive matches
      consecutive_matches = 0;
    }
  }
  
  // Must match all pattern characters
  if (pattern_pos < pattern_len) return 0;
  
  // Apply first character bonus
  score += first_char_bonus;
  
  // Bonus for shorter filenames (prefer more specific matches)
  score += (100 - filename_len);
  
  // Bonus for fewer unmatched characters
  int unmatched_chars = filename_len - pattern_len;
  score += (50 - unmatched_chars);
  
  return score;
}

/**
 * Compare function for sorting scored files (higher scores first)
 */
int compare_scored_files(const void *a, const void *b) {
  const struct {int file_index; int score;} *file_a = a;
  const struct {int file_index; int score;} *file_b = b;
  return file_b->score - file_a->score; // Descending order (higher scores first)
}

/**
 * Update fuzzy search filter based on current query
 */
void update_fuzzy_filter(NCursesDiffViewer *viewer) {
  if (!viewer) return;
  
  viewer->fuzzy_filtered_count = 0;
  viewer->fuzzy_selected_index = 0;
  viewer->fuzzy_scroll_offset = 0;
  
  // Calculate scores for all files and collect matches
  for (int i = 0; i < viewer->file_count && viewer->fuzzy_filtered_count < MAX_FILES; i++) {
    int score = calculate_fuzzy_score(viewer->fuzzy_search_query, viewer->files[i].filename);
    if (score > 0) {
      viewer->fuzzy_scored_files[viewer->fuzzy_filtered_count].file_index = i;
      viewer->fuzzy_scored_files[viewer->fuzzy_filtered_count].score = score;
      viewer->fuzzy_filtered_count++;
    }
  }
  
  // Sort results by score (highest first)
  if (viewer->fuzzy_filtered_count > 1) {
    qsort(viewer->fuzzy_scored_files, viewer->fuzzy_filtered_count, 
          sizeof(viewer->fuzzy_scored_files[0]), compare_scored_files);
  }
}

/**
 * Enter fuzzy search mode
 */
void enter_fuzzy_search_mode(NCursesDiffViewer *viewer) {
  if (!viewer || viewer->current_mode != NCURSES_MODE_FILE_LIST) return;
  
  viewer->fuzzy_search_active = 1;
  viewer->fuzzy_search_query[0] = '\0';
  viewer->fuzzy_search_query_len = 0;
  
  // Create fuzzy search windows
  int input_height = 3;
  int list_height = viewer->terminal_height - input_height - 4;
  int width = viewer->terminal_width - 4;
  int start_y = (viewer->terminal_height - input_height - list_height) / 2;
  int start_x = 2;
  
  viewer->fuzzy_input_win = newwin(input_height, width, start_y, start_x);
  viewer->fuzzy_list_win = newwin(list_height, width, start_y + input_height, start_x);
  
  if (viewer->fuzzy_input_win) {
    box(viewer->fuzzy_input_win, 0, 0);
    mvwprintw(viewer->fuzzy_input_win, 0, 2, " Fuzzy File Search ");
  }
  
  if (viewer->fuzzy_list_win) {
    box(viewer->fuzzy_list_win, 0, 0);
  }
  
  // Initialize with all files
  update_fuzzy_filter(viewer);
  
  // Force initial full redraw
  viewer->fuzzy_needs_full_redraw = 1;
}

/**
 * Exit fuzzy search mode
 */
void exit_fuzzy_search_mode(NCursesDiffViewer *viewer) {
  if (!viewer) return;
  
  viewer->fuzzy_search_active = 0;
  cleanup_fuzzy_search(viewer);
  
  // Force redraw of main windows next time
  touchwin(stdscr);
  refresh();
}

/**
 * Render fuzzy search input field only
 */
void render_fuzzy_input(NCursesDiffViewer *viewer) {
  if (!viewer->fuzzy_input_win) return;
  
  // Clear only the content area, not the border
  for (int y = 1; y < getmaxy(viewer->fuzzy_input_win) - 1; y++) {
    for (int x = 1; x < getmaxx(viewer->fuzzy_input_win) - 1; x++) {
      mvwaddch(viewer->fuzzy_input_win, y, x, ' ');
    }
  }
  
  // Show current query
  mvwprintw(viewer->fuzzy_input_win, 1, 2, "> %s", viewer->fuzzy_search_query);
  
  // Show cursor
  int cursor_x = 4 + viewer->fuzzy_search_query_len;
  if (cursor_x < getmaxx(viewer->fuzzy_input_win) - 1) {
    mvwaddch(viewer->fuzzy_input_win, 1, cursor_x, '_');
  }
  
  wrefresh(viewer->fuzzy_input_win);
}

/**
 * Render fuzzy search file list content only (no borders)
 */
void render_fuzzy_list_content(NCursesDiffViewer *viewer) {
  if (!viewer->fuzzy_list_win) return;
  
  int list_height = getmaxy(viewer->fuzzy_list_win) - 2;
  int list_width = getmaxx(viewer->fuzzy_list_win) - 4;
  
  // Clear only the content area, not the border
  for (int y = 1; y <= list_height; y++) {
    for (int x = 1; x < getmaxx(viewer->fuzzy_list_win) - 1; x++) {
      mvwaddch(viewer->fuzzy_list_win, y, x, ' ');
    }
  }
  
  // Show filtered results
  for (int i = 0; i < viewer->fuzzy_filtered_count && i < list_height; i++) {
    int display_index = i + viewer->fuzzy_scroll_offset;
    if (display_index >= viewer->fuzzy_filtered_count) break;
    
    int file_index = viewer->fuzzy_scored_files[display_index].file_index;
    char *filename = viewer->files[file_index].filename;
    char status = viewer->files[file_index].status;
    
    // Highlight selected item
    if (display_index == viewer->fuzzy_selected_index) {
      wattron(viewer->fuzzy_list_win, A_REVERSE);
    }
    
    // Show status character and filename
    mvwprintw(viewer->fuzzy_list_win, i + 1, 2, "%c %-*.*s", 
              status, list_width - 3, list_width - 3, filename);
    
    if (display_index == viewer->fuzzy_selected_index) {
      wattroff(viewer->fuzzy_list_win, A_REVERSE);
    }
  }
  
  // Show result count in top border area
  if (viewer->fuzzy_filtered_count > 0) {
    mvwprintw(viewer->fuzzy_list_win, 0, getmaxx(viewer->fuzzy_list_win) - 15, 
              " %d/%d ", viewer->fuzzy_selected_index + 1, viewer->fuzzy_filtered_count);
  } else {
    mvwprintw(viewer->fuzzy_list_win, 0, getmaxx(viewer->fuzzy_list_win) - 10, " 0/0 ");
  }
  
  wrefresh(viewer->fuzzy_list_win);
}

/**
 * Create fuzzy search windows with borders (one-time setup)
 */
void create_fuzzy_windows_with_borders(NCursesDiffViewer *viewer) {
  if (!viewer->fuzzy_input_win || !viewer->fuzzy_list_win) return;
  
  // Draw input window border and title
  wclear(viewer->fuzzy_input_win);
  box(viewer->fuzzy_input_win, 0, 0);
  mvwprintw(viewer->fuzzy_input_win, 0, 2, " Fuzzy File Search ");
  wrefresh(viewer->fuzzy_input_win);
  
  // Draw list window border
  wclear(viewer->fuzzy_list_win);
  box(viewer->fuzzy_list_win, 0, 0);
  wrefresh(viewer->fuzzy_list_win);
}

/**
 * Main fuzzy search render function with granular updates
 */
void render_fuzzy_search(NCursesDiffViewer *viewer) {
  if (!viewer || !viewer->fuzzy_search_active) return;
  if (!viewer->fuzzy_input_win || !viewer->fuzzy_list_win) return;
  
  // Check what specifically needs to be redrawn
  int query_changed = strcmp(viewer->fuzzy_search_query, viewer->fuzzy_last_query) != 0;
  int selection_changed = viewer->fuzzy_selected_index != viewer->fuzzy_last_selected;
  int scroll_changed = viewer->fuzzy_scroll_offset != viewer->fuzzy_last_scroll;
  int results_changed = viewer->fuzzy_filtered_count != viewer->fuzzy_last_filtered_count;
  
  // Full redraw needed on first render
  if (viewer->fuzzy_needs_full_redraw) {
    create_fuzzy_windows_with_borders(viewer);
    render_fuzzy_input(viewer);
    render_fuzzy_list_content(viewer);
    viewer->fuzzy_needs_full_redraw = 0;
  }
  // Redraw input field if query changed
  else if (query_changed || viewer->fuzzy_needs_input_redraw) {
    render_fuzzy_input(viewer);
    viewer->fuzzy_needs_input_redraw = 0;
  }
  
  // Redraw file list if results, selection, or scroll changed
  if (results_changed || selection_changed || scroll_changed || viewer->fuzzy_needs_list_redraw) {
    render_fuzzy_list_content(viewer);
    viewer->fuzzy_needs_list_redraw = 0;
  }
  
  // Update last rendered state
  strcpy(viewer->fuzzy_last_query, viewer->fuzzy_search_query);
  viewer->fuzzy_last_selected = viewer->fuzzy_selected_index;
  viewer->fuzzy_last_scroll = viewer->fuzzy_scroll_offset;
  viewer->fuzzy_last_filtered_count = viewer->fuzzy_filtered_count;
}

/**
 * Handle fuzzy search input
 */
int handle_fuzzy_search_input(NCursesDiffViewer *viewer, int key) {
  if (!viewer || !viewer->fuzzy_search_active) return 0;
  
  switch (key) {
    case 27: // ESC
      exit_fuzzy_search_mode(viewer);
      return 1;
      
    case KEY_ENTER:
    case '\n':
    case '\r':
      if (viewer->fuzzy_filtered_count > 0) {
        select_fuzzy_file(viewer);
        exit_fuzzy_search_mode(viewer);
      }
      return 1;
      
    case KEY_UP:
      if (viewer->fuzzy_selected_index > 0) {
        viewer->fuzzy_selected_index--;
        // Adjust scroll if needed
        if (viewer->fuzzy_selected_index < viewer->fuzzy_scroll_offset) {
          viewer->fuzzy_scroll_offset = viewer->fuzzy_selected_index;
        }
      }
      return 1;
      
    case KEY_DOWN:
      if (viewer->fuzzy_selected_index < viewer->fuzzy_filtered_count - 1) {
        viewer->fuzzy_selected_index++;
        // Adjust scroll if needed
        int list_height = getmaxy(viewer->fuzzy_list_win) - 2;
        if (viewer->fuzzy_selected_index >= viewer->fuzzy_scroll_offset + list_height) {
          viewer->fuzzy_scroll_offset = viewer->fuzzy_selected_index - list_height + 1;
        }
      }
      return 1;
      
    case KEY_BACKSPACE:
    case 127:
    case '\b':
      if (viewer->fuzzy_search_query_len > 0) {
        viewer->fuzzy_search_query_len--;
        viewer->fuzzy_search_query[viewer->fuzzy_search_query_len] = '\0';
        update_fuzzy_filter(viewer);
      }
      return 1;
      
    default:
      // Add printable characters to search query
      if (key >= 32 && key <= 126 && viewer->fuzzy_search_query_len < 255) {
        viewer->fuzzy_search_query[viewer->fuzzy_search_query_len++] = key;
        viewer->fuzzy_search_query[viewer->fuzzy_search_query_len] = '\0';
        update_fuzzy_filter(viewer);
      }
      return 1;
  }
}

/**
 * Select file from fuzzy search results
 */
void select_fuzzy_file(NCursesDiffViewer *viewer) {
  if (!viewer || viewer->fuzzy_filtered_count == 0) return;
  
  // Get the actual file index from scored results
  int file_index = viewer->fuzzy_scored_files[viewer->fuzzy_selected_index].file_index;
  
  // Update the main file list selection to point to this file
  viewer->selected_file = file_index;
  
  // Scroll the main file list to show the selected file
  int file_panel_height = getmaxy(viewer->file_list_win) - 2;
  if (viewer->selected_file < viewer->file_count) {
    // Ensure the selected file is visible in the main panel
    if (viewer->selected_file < file_panel_height / 2) {
      // If near the top, scroll to top
      // No action needed as the file list starts from 0
    } else if (viewer->selected_file >= viewer->file_count - file_panel_height / 2) {
      // If near the bottom, show as much as possible
      // No action needed as we'll handle this in rendering
    }
  }
  
  // Load the selected file with split staging view (same as pressing Enter)
  if (viewer->selected_file >= 0 && viewer->selected_file < viewer->file_count) {
    const char *filename = viewer->files[viewer->selected_file].filename;
    
    // Load file with staging information
    load_file_with_staging_info(viewer, filename);
    
    // Switch to split view mode
    viewer->split_view_mode = 1;
    viewer->current_mode = NCURSES_MODE_FILE_VIEW;
    viewer->active_pane = 0; // Start with unstaged pane active
    
    // Store current file path
    strncpy(viewer->current_file_path, filename, sizeof(viewer->current_file_path) - 1);
    viewer->current_file_path[sizeof(viewer->current_file_path) - 1] = '\0';
  }
}

/**
 * Initialize grep search state
 */
void init_grep_search(NCursesDiffViewer *viewer) {
  if (!viewer) return;
  
  viewer->grep_search_active = 0;
  viewer->grep_search_mode = NCURSES_MODE_FILE_LIST;
  viewer->grep_search_query[0] = '\0';
  viewer->grep_search_query_len = 0;
  viewer->grep_filtered_count = 0;
  viewer->grep_selected_index = 0;
  viewer->grep_scroll_offset = 0;
  viewer->grep_input_win = NULL;
  viewer->grep_list_win = NULL;
  
  // Initialize state tracking
  viewer->grep_needs_full_redraw = 0;
  viewer->grep_needs_input_redraw = 0;
  viewer->grep_needs_list_redraw = 0;
  viewer->grep_last_query[0] = '\0';
  viewer->grep_last_selected = -1;
  viewer->grep_last_scroll = -1;
  viewer->grep_last_filtered_count = -1;
}

/**
 * Cleanup grep search windows
 */
void cleanup_grep_search(NCursesDiffViewer *viewer) {
  if (!viewer) return;
  
  if (viewer->grep_input_win) {
    delwin(viewer->grep_input_win);
    viewer->grep_input_win = NULL;
  }
  if (viewer->grep_list_win) {
    delwin(viewer->grep_list_win);
    viewer->grep_list_win = NULL;
  }
}

/**
 * Extract branch name from stash info
 */
void extract_branch_from_stash(const char *stash_info, char *branch_name, int max_len) {
  if (!stash_info || !branch_name || max_len <= 0) return;
  
  branch_name[0] = '\0';
  
  // Look for "On " pattern in stash info
  const char *on_pattern = "On ";
  const char *on_pos = strstr(stash_info, on_pattern);
  
  if (on_pos) {
    const char *branch_start = on_pos + strlen(on_pattern);
    const char *branch_end = strchr(branch_start, ':');
    
    if (branch_end && branch_end > branch_start) {
      int branch_len = branch_end - branch_start;
      if (branch_len < max_len) {
        strncpy(branch_name, branch_start, branch_len);
        branch_name[branch_len] = '\0';
        
        // Trim trailing spaces
        while (branch_len > 0 && branch_name[branch_len - 1] == ' ') {
          branch_name[--branch_len] = '\0';
        }
      }
    }
  }
}

/**
 * Calculate grep match score for text content
 */
int calculate_grep_score(const char *pattern, const char *text) {
  if (!pattern || !text) return 0;
  if (strlen(pattern) == 0) return 1000; // Empty pattern matches everything
  
  int pattern_len = strlen(pattern);
  int text_len = strlen(text);
  
  if (pattern_len > text_len) return 0;
  
  int score = 0;
  int pattern_pos = 0;
  int consecutive_matches = 0;
  int first_char_bonus = 0;
  
  for (int i = 0; i < text_len && pattern_pos < pattern_len; i++) {
    char p_char = tolower(pattern[pattern_pos]);
    char t_char = tolower(text[i]);
    
    if (p_char == t_char) {
      // Base score for character match
      score += 1;
      
      // Bonus for consecutive matches
      consecutive_matches++;
      score += consecutive_matches * 3;
      
      // Bonus for matching at word boundaries
      if (i == 0 || text[i-1] == ' ' || text[i-1] == '-' || text[i-1] == '_') {
        score += 10;
      }
      
      // Bonus for matching first character
      if (pattern_pos == 0 && i == 0) {
        first_char_bonus = 30;
      }
      
      // Bonus for exact position match
      if (i == pattern_pos) {
        score += 5;
      }
      
      pattern_pos++;
    } else {
      consecutive_matches = 0;
    }
  }
  
  // Must match all pattern characters
  if (pattern_pos < pattern_len) return 0;
  
  score += first_char_bonus;
  score += (50 - text_len); // Prefer shorter text
  score += (30 - (text_len - pattern_len)); // Fewer unmatched chars
  
  return score;
}

/**
 * Compare function for sorting grep scored items
 */
int compare_grep_scored_items(const void *a, const void *b) {
  const struct {int item_index; int score;} *item_a = a;
  const struct {int item_index; int score;} *item_b = b;
  return item_b->score - item_a->score; // Descending order
}

/**
 * Update grep search filter based on current query and mode
 */
void update_grep_filter(NCursesDiffViewer *viewer) {
  if (!viewer) return;
  
  viewer->grep_filtered_count = 0;
  viewer->grep_selected_index = 0;
  viewer->grep_scroll_offset = 0;
  
  // Search different data based on current mode
  switch (viewer->grep_search_mode) {
    case NCURSES_MODE_COMMIT_LIST:
      // Search commit titles and author initials
      for (int i = 0; i < viewer->commit_count && viewer->grep_filtered_count < MAX_COMMITS; i++) {
        // Try matching against title first
        int title_score = calculate_grep_score(viewer->grep_search_query, viewer->commits[i].title);
        
        // Try matching against author initials
        int author_score = calculate_grep_score(viewer->grep_search_query, viewer->commits[i].author_initials);
        
        // Use the higher score (prefer title matches over author matches)
        int score = (title_score > author_score) ? title_score : author_score;
        
        if (score > 0) {
          viewer->grep_scored_items[viewer->grep_filtered_count].item_index = i;
          viewer->grep_scored_items[viewer->grep_filtered_count].score = score;
          viewer->grep_filtered_count++;
        }
      }
      break;
      
    case NCURSES_MODE_STASH_LIST:
      // Search stash info and branch names
      for (int i = 0; i < viewer->stash_count && viewer->grep_filtered_count < MAX_COMMITS; i++) {
        // Try matching against full stash info first
        int stash_score = calculate_grep_score(viewer->grep_search_query, viewer->stashes[i].stash_info);
        
        // Extract and try matching against branch name
        char branch_name[64];
        extract_branch_from_stash(viewer->stashes[i].stash_info, branch_name, sizeof(branch_name));
        int branch_score = 0;
        if (strlen(branch_name) > 0) {
          branch_score = calculate_grep_score(viewer->grep_search_query, branch_name);
        }
        
        // Use the higher score (prefer full stash info matches over branch-only matches)
        int score = (stash_score > branch_score) ? stash_score : branch_score;
        
        if (score > 0) {
          viewer->grep_scored_items[viewer->grep_filtered_count].item_index = i;
          viewer->grep_scored_items[viewer->grep_filtered_count].score = score;
          viewer->grep_filtered_count++;
        }
      }
      break;
      
    case NCURSES_MODE_BRANCH_LIST:
      // Search branch names
      for (int i = 0; i < viewer->branch_count && viewer->grep_filtered_count < MAX_COMMITS; i++) {
        int score = calculate_grep_score(viewer->grep_search_query, viewer->branches[i].name);
        if (score > 0) {
          viewer->grep_scored_items[viewer->grep_filtered_count].item_index = i;
          viewer->grep_scored_items[viewer->grep_filtered_count].score = score;
          viewer->grep_filtered_count++;
        }
      }
      break;
      
    default:
      // No grep search for other modes
      break;
  }
  
  // Sort results by score
  if (viewer->grep_filtered_count > 1) {
    qsort(viewer->grep_scored_items, viewer->grep_filtered_count,
          sizeof(viewer->grep_scored_items[0]), compare_grep_scored_items);
  }
}

/**
 * Enter grep search mode for current view mode
 */
void enter_grep_search_mode(NCursesDiffViewer *viewer) {
  if (!viewer) return;
  
  // Only allow grep search in specific modes
  if (viewer->current_mode != NCURSES_MODE_COMMIT_LIST &&
      viewer->current_mode != NCURSES_MODE_STASH_LIST &&
      viewer->current_mode != NCURSES_MODE_BRANCH_LIST) {
    return;
  }
  
  viewer->grep_search_active = 1;
  viewer->grep_search_mode = viewer->current_mode;
  viewer->grep_search_query[0] = '\0';
  viewer->grep_search_query_len = 0;
  
  // Create grep search windows
  int input_height = 3;
  int list_height = viewer->terminal_height - input_height - 4;
  int width = viewer->terminal_width - 4;
  int start_y = (viewer->terminal_height - input_height - list_height) / 2;
  int start_x = 2;
  
  viewer->grep_input_win = newwin(input_height, width, start_y, start_x);
  viewer->grep_list_win = newwin(list_height, width, start_y + input_height, start_x);
  
  // Initialize with all items
  update_grep_filter(viewer);
  
  // Force initial full redraw
  viewer->grep_needs_full_redraw = 1;
}

/**
 * Exit grep search mode
 */
void exit_grep_search_mode(NCursesDiffViewer *viewer) {
  if (!viewer) return;
  
  viewer->grep_search_active = 0;
  cleanup_grep_search(viewer);
  
  // Force redraw of main windows next time
  touchwin(stdscr);
  refresh();
}

/**
 * Render grep search input field only
 */
void render_grep_input(NCursesDiffViewer *viewer) {
  if (!viewer->grep_input_win) return;
  
  // Clear only the content area, not the border
  for (int y = 1; y < getmaxy(viewer->grep_input_win) - 1; y++) {
    for (int x = 1; x < getmaxx(viewer->grep_input_win) - 1; x++) {
      mvwaddch(viewer->grep_input_win, y, x, ' ');
    }
  }
  
  // Show current query
  mvwprintw(viewer->grep_input_win, 1, 2, "> %s", viewer->grep_search_query);
  
  // Show cursor
  int cursor_x = 4 + viewer->grep_search_query_len;
  if (cursor_x < getmaxx(viewer->grep_input_win) - 1) {
    mvwaddch(viewer->grep_input_win, 1, cursor_x, '_');
  }
  
  wrefresh(viewer->grep_input_win);
}

/**
 * Render grep search list content only (no borders)
 */
void render_grep_list_content(NCursesDiffViewer *viewer) {
  if (!viewer->grep_list_win) return;
  
  int list_height = getmaxy(viewer->grep_list_win) - 2;
  int list_width = getmaxx(viewer->grep_list_win) - 4;
  
  // Clear only the content area, not the border
  for (int y = 1; y <= list_height; y++) {
    for (int x = 1; x < getmaxx(viewer->grep_list_win) - 1; x++) {
      mvwaddch(viewer->grep_list_win, y, x, ' ');
    }
  }
  
  // Show filtered results
  for (int i = 0; i < viewer->grep_filtered_count && i < list_height; i++) {
    int display_index = i + viewer->grep_scroll_offset;
    if (display_index >= viewer->grep_filtered_count) break;
    
    int item_index = viewer->grep_scored_items[display_index].item_index;
    char display_text[512] = "";
    
    // Get display text based on mode
    switch (viewer->grep_search_mode) {
      case NCURSES_MODE_COMMIT_LIST:
        // For commits, we'll handle colors separately
        break;
      case NCURSES_MODE_STASH_LIST:
        // For stashes, we'll handle special formatting to highlight branch
        break;
      case NCURSES_MODE_BRANCH_LIST:
        strncpy(display_text, viewer->branches[item_index].name, sizeof(display_text) - 1);
        break;
      default:
        strcpy(display_text, "Unknown");
        break;
    }
    display_text[sizeof(display_text) - 1] = '\0';
    
    // Highlight selected item
    if (display_index == viewer->grep_selected_index) {
      wattron(viewer->grep_list_win, A_REVERSE);
    }
    
    // Render based on mode with appropriate colors
    if (viewer->grep_search_mode == NCURSES_MODE_COMMIT_LIST) {
      // Render commit with colors: hash (yellow) + author initials (green) + title
      int x_pos = 2;
      
      // Hash in yellow (COLOR_PAIR(10))
      wattron(viewer->grep_list_win, COLOR_PAIR(10));
      mvwprintw(viewer->grep_list_win, i + 1, x_pos, "%s", viewer->commits[item_index].hash);
      wattroff(viewer->grep_list_win, COLOR_PAIR(10));
      x_pos += strlen(viewer->commits[item_index].hash) + 1;
      
      // Author initials in green (COLOR_PAIR(8))
      wattron(viewer->grep_list_win, COLOR_PAIR(8));
      mvwprintw(viewer->grep_list_win, i + 1, x_pos, "%s", viewer->commits[item_index].author_initials);
      wattroff(viewer->grep_list_win, COLOR_PAIR(8));
      x_pos += strlen(viewer->commits[item_index].author_initials) + 1;
      
      // Title in default color
      int remaining_width = list_width - (x_pos - 2);
      if (remaining_width > 0) {
        mvwprintw(viewer->grep_list_win, i + 1, x_pos, "%-*.*s", 
                  remaining_width, remaining_width, viewer->commits[item_index].title);
      }
    } else if (viewer->grep_search_mode == NCURSES_MODE_STASH_LIST) {
      // Render stash with branch name highlighted
      const char *stash_info = viewer->stashes[item_index].stash_info;
      char branch_name[64];
      extract_branch_from_stash(stash_info, branch_name, sizeof(branch_name));
      
      // Find the "On branch_name:" part to highlight
      char on_pattern[80];
      snprintf(on_pattern, sizeof(on_pattern), "On %s:", branch_name);
      const char *on_pos = strstr(stash_info, on_pattern);
      
      if (on_pos && strlen(branch_name) > 0) {
        int x_pos = 2;
        
        // Render text before "On"
        int before_len = on_pos - stash_info;
        if (before_len > 0) {
          mvwprintw(viewer->grep_list_win, i + 1, x_pos, "%.*s", before_len, stash_info);
          x_pos += before_len;
        }
        
        // Render "On " in default color
        mvwprintw(viewer->grep_list_win, i + 1, x_pos, "On ");
        x_pos += 3;
        
        // Render branch name in green (COLOR_PAIR(8))
        wattron(viewer->grep_list_win, COLOR_PAIR(8));
        mvwprintw(viewer->grep_list_win, i + 1, x_pos, "%s", branch_name);
        wattroff(viewer->grep_list_win, COLOR_PAIR(8));
        x_pos += strlen(branch_name);
        
        // Render remaining text after branch name
        const char *after_pos = on_pos + strlen(on_pattern);
        int remaining_width = list_width - (x_pos - 2);
        if (remaining_width > 0 && *after_pos) {
          mvwprintw(viewer->grep_list_win, i + 1, x_pos, ":%-*.*s", 
                    remaining_width - 1, remaining_width - 1, after_pos);
        }
      } else {
        // Fallback: render normally if no branch pattern found
        mvwprintw(viewer->grep_list_win, i + 1, 2, "%-*.*s", 
                  list_width - 2, list_width - 2, stash_info);
      }
    } else {
      // Show item text for other modes (branches)
      mvwprintw(viewer->grep_list_win, i + 1, 2, "%-*.*s", 
                list_width - 2, list_width - 2, display_text);
    }
    
    if (display_index == viewer->grep_selected_index) {
      wattroff(viewer->grep_list_win, A_REVERSE);
    }
  }
  
  // Show result count
  if (viewer->grep_filtered_count > 0) {
    mvwprintw(viewer->grep_list_win, 0, getmaxx(viewer->grep_list_win) - 15, 
              " %d/%d ", viewer->grep_selected_index + 1, viewer->grep_filtered_count);
  } else {
    mvwprintw(viewer->grep_list_win, 0, getmaxx(viewer->grep_list_win) - 10, " 0/0 ");
  }
  
  wrefresh(viewer->grep_list_win);
}

/**
 * Create grep search windows with borders (one-time setup)
 */
void create_grep_windows_with_borders(NCursesDiffViewer *viewer) {
  if (!viewer->grep_input_win || !viewer->grep_list_win) return;
  
  // Get mode name for title
  const char *mode_name = "Search";
  switch (viewer->grep_search_mode) {
    case NCURSES_MODE_COMMIT_LIST:
      mode_name = "Commit Grep";
      break;
    case NCURSES_MODE_STASH_LIST:
      mode_name = "Stash Grep";
      break;
    case NCURSES_MODE_BRANCH_LIST:
      mode_name = "Branch Grep";
      break;
    default:
      break;
  }
  
  // Draw input window border and title
  wclear(viewer->grep_input_win);
  box(viewer->grep_input_win, 0, 0);
  mvwprintw(viewer->grep_input_win, 0, 2, " %s ", mode_name);
  wrefresh(viewer->grep_input_win);
  
  // Draw list window border
  wclear(viewer->grep_list_win);
  box(viewer->grep_list_win, 0, 0);
  wrefresh(viewer->grep_list_win);
}

/**
 * Main grep search render function with granular updates
 */
void render_grep_search(NCursesDiffViewer *viewer) {
  if (!viewer || !viewer->grep_search_active) return;
  if (!viewer->grep_input_win || !viewer->grep_list_win) return;
  
  // Check what specifically needs to be redrawn
  int query_changed = strcmp(viewer->grep_search_query, viewer->grep_last_query) != 0;
  int selection_changed = viewer->grep_selected_index != viewer->grep_last_selected;
  int scroll_changed = viewer->grep_scroll_offset != viewer->grep_last_scroll;
  int results_changed = viewer->grep_filtered_count != viewer->grep_last_filtered_count;
  
  // Full redraw needed on first render
  if (viewer->grep_needs_full_redraw) {
    create_grep_windows_with_borders(viewer);
    render_grep_input(viewer);
    render_grep_list_content(viewer);
    viewer->grep_needs_full_redraw = 0;
  }
  // Redraw input field if query changed
  else if (query_changed || viewer->grep_needs_input_redraw) {
    render_grep_input(viewer);
    viewer->grep_needs_input_redraw = 0;
  }
  
  // Redraw list if results, selection, or scroll changed
  if (results_changed || selection_changed || scroll_changed || viewer->grep_needs_list_redraw) {
    render_grep_list_content(viewer);
    viewer->grep_needs_list_redraw = 0;
  }
  
  // Update last rendered state
  strcpy(viewer->grep_last_query, viewer->grep_search_query);
  viewer->grep_last_selected = viewer->grep_selected_index;
  viewer->grep_last_scroll = viewer->grep_scroll_offset;
  viewer->grep_last_filtered_count = viewer->grep_filtered_count;
}

/**
 * Handle grep search input
 */
int handle_grep_search_input(NCursesDiffViewer *viewer, int key) {
  if (!viewer || !viewer->grep_search_active) return 0;
  
  switch (key) {
    case 27: // ESC
      exit_grep_search_mode(viewer);
      return 1;
      
    case KEY_ENTER:
    case '\n':
    case '\r':
      if (viewer->grep_filtered_count > 0) {
        select_grep_item(viewer);
        exit_grep_search_mode(viewer);
      }
      return 1;
      
    case KEY_UP:
      if (viewer->grep_selected_index > 0) {
        viewer->grep_selected_index--;
        // Adjust scroll if needed
        if (viewer->grep_selected_index < viewer->grep_scroll_offset) {
          viewer->grep_scroll_offset = viewer->grep_selected_index;
        }
      }
      return 1;
      
    case KEY_DOWN:
      if (viewer->grep_selected_index < viewer->grep_filtered_count - 1) {
        viewer->grep_selected_index++;
        // Adjust scroll if needed
        int list_height = getmaxy(viewer->grep_list_win) - 2;
        if (viewer->grep_selected_index >= viewer->grep_scroll_offset + list_height) {
          viewer->grep_scroll_offset = viewer->grep_selected_index - list_height + 1;
        }
      }
      return 1;
      
    case KEY_BACKSPACE:
    case 127:
    case '\b':
      if (viewer->grep_search_query_len > 0) {
        viewer->grep_search_query_len--;
        viewer->grep_search_query[viewer->grep_search_query_len] = '\0';
        update_grep_filter(viewer);
      }
      return 1;
      
    default:
      // Add printable characters to search query
      if (key >= 32 && key <= 126 && viewer->grep_search_query_len < 255) {
        viewer->grep_search_query[viewer->grep_search_query_len++] = key;
        viewer->grep_search_query[viewer->grep_search_query_len] = '\0';
        update_grep_filter(viewer);
      }
      return 1;
  }
}

/**
 * Select item from grep search results
 */
void select_grep_item(NCursesDiffViewer *viewer) {
  if (!viewer || viewer->grep_filtered_count == 0) return;
  
  // Get the actual item index from scored results
  int item_index = viewer->grep_scored_items[viewer->grep_selected_index].item_index;
  
  // Update the main selection based on mode
  switch (viewer->grep_search_mode) {
    case NCURSES_MODE_COMMIT_LIST:
      viewer->selected_commit = item_index;
      viewer->current_mode = NCURSES_MODE_COMMIT_VIEW;
      // Load the selected commit for viewing
      if (item_index >= 0 && item_index < viewer->commit_count) {
        load_commit_for_viewing(viewer, viewer->commits[item_index].hash);
      }
      break;
      
    case NCURSES_MODE_STASH_LIST:
      viewer->selected_stash = item_index;
      viewer->current_mode = NCURSES_MODE_STASH_VIEW;
      // Load the selected stash for viewing
      if (item_index >= 0 && item_index < viewer->stash_count) {
        load_stash_for_viewing(viewer, item_index);
      }
      break;
      
    case NCURSES_MODE_BRANCH_LIST:
      viewer->selected_branch = item_index;
      viewer->current_mode = NCURSES_MODE_BRANCH_VIEW;
      // Load commits for the selected branch
      if (item_index >= 0 && item_index < viewer->branch_count) {
        load_branch_commits(viewer, viewer->branches[item_index].name);
        parse_branch_commits_to_lines(viewer);
      }
      break;
      
    default:
      break;
  }
}
