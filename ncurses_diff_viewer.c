/**
 * ncurses_diff_viewer.c
 * NCurses-based interactive diff viewer implementation
 */

#include "ncurses_diff_viewer.h"
#include "git_integration.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/**
 * Initialize the ncurses diff viewer
 */
int init_ncurses_diff_viewer(NCursesDiffViewer *viewer) {
  if (!viewer)
    return 0;

  memset(viewer, 0, sizeof(NCursesDiffViewer));
  viewer->selected_file = 0;
  viewer->file_scroll_offset = 0;
  viewer->selected_stash = 0;
  viewer->current_mode = NCURSES_MODE_FILE_LIST;
  viewer->sync_status = SYNC_STATUS_IDLE;
  viewer->spinner_frame = 0;
  viewer->last_sync_time = time(NULL);
  viewer->animation_frame = 0;
  viewer->text_char_count = 0;

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
    init_pair(1, COLOR_GREEN, COLOR_BLACK);  // Additions
    init_pair(2, COLOR_RED, COLOR_BLACK);    // Deletions
    init_pair(3, COLOR_CYAN, COLOR_BLACK);   // Headers
    init_pair(4, COLOR_YELLOW, COLOR_BLACK); // Selected
    init_pair(5, COLOR_WHITE, COLOR_BLUE);   // Highlighted selection
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
  viewer->file_panel_height = available_height * 0.4;
  viewer->commit_panel_height = available_height * 0.4;
  viewer->stash_panel_height = available_height - viewer->file_panel_height -
                               viewer->commit_panel_height - 2;

  // Position status bar right after the main content
  int status_bar_y = 1 + available_height;

  // Create five windows
  viewer->file_list_win =
      newwin(viewer->file_panel_height, viewer->file_panel_width, 1, 0);
  viewer->commit_list_win =
      newwin(viewer->commit_panel_height, viewer->file_panel_width,
             1 + viewer->file_panel_height + 1, 0);
  viewer->stash_list_win = newwin(
      viewer->stash_panel_height, viewer->file_panel_width,
      1 + viewer->file_panel_height + 1 + viewer->commit_panel_height + 1, 0);
  viewer->file_content_win = newwin(
      available_height, viewer->terminal_width - viewer->file_panel_width - 1,
      1, viewer->file_panel_width + 1);
  viewer->status_bar_win = newwin(viewer->status_bar_height,
                                  viewer->terminal_width, status_bar_y, 0);

  if (!viewer->file_list_win || !viewer->file_content_win ||
      !viewer->commit_list_win || !viewer->stash_list_win ||
      !viewer->status_bar_win) {
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
    // Remove newline
    char *newline = strchr(line, '\n');
    if (newline)
      *newline = '\0';

    if (strlen(line) < 3)
      continue;

    // Parse git status format: "XY filename"
    char status = line[0];
    if (status == ' ')
      status = line[1]; // Check second column if first is space

    // Skip the status characters and space
    char *filename = line + 3;

    // Store file info
    viewer->files[viewer->file_count].status = status;
    viewer->files[viewer->file_count].marked_for_commit =
        0; // Not marked by default
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
 * Load diff context with highlighting (like lazygit)
 */
int load_full_file_with_diff(NCursesDiffViewer *viewer, const char *filename) {
  if (!viewer || !filename)
    return 0;

  viewer->file_line_count = 0;
  viewer->file_scroll_offset = 0;

  // Check if this is a new file
  if (is_ncurses_new_file(filename)) {
    // For new files, show first 50 lines as additions
    FILE *fp = fopen(filename, "r");
    if (!fp)
      return 0;

    char line[1024];
    int line_count = 0;
    while (fgets(line, sizeof(line), fp) != NULL &&
           viewer->file_line_count < MAX_FULL_FILE_LINES && line_count < 50) {
      // Remove newline
      char *newline_pos = strchr(line, '\n');
      if (newline_pos)
        *newline_pos = '\0';

      NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
      snprintf(file_line->line, sizeof(file_line->line), "+%s", line);
      file_line->type = '+';
      file_line->is_diff_line = 1;

      viewer->file_line_count++;
      line_count++;
    }

    fclose(fp);
    return viewer->file_line_count;
  }

  // Use git diff to get only the changed context (like lazygit)
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "git diff HEAD \"%s\" 2>/dev/null", filename);

  FILE *diff_fp = popen(cmd, "r");
  if (!diff_fp)
    return 0;

  char diff_line[1024];
  int found_changes = 0;

  while (fgets(diff_line, sizeof(diff_line), diff_fp) != NULL &&
         viewer->file_line_count < MAX_FULL_FILE_LINES) {
    // Remove newline
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

    // Process hunk headers and content
    if (diff_line[0] == '@' && diff_line[1] == '@') {
      // Hunk header
      NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
      strncpy(file_line->line, diff_line, sizeof(file_line->line) - 1);
      file_line->line[sizeof(file_line->line) - 1] = '\0';
      file_line->type = '@';
      file_line->is_diff_line = 0;
      viewer->file_line_count++;
      found_changes = 1;
    } else if (diff_line[0] == '+') {
      // Added line
      NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
      strncpy(file_line->line, diff_line, sizeof(file_line->line) - 1);
      file_line->line[sizeof(file_line->line) - 1] = '\0';
      file_line->type = '+';
      file_line->is_diff_line = 1;
      viewer->file_line_count++;
      found_changes = 1;
    } else if (diff_line[0] == '-') {
      // Removed line
      NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
      strncpy(file_line->line, diff_line, sizeof(file_line->line) - 1);
      file_line->line[sizeof(file_line->line) - 1] = '\0';
      file_line->type = '-';
      file_line->is_diff_line = 1;
      viewer->file_line_count++;
      found_changes = 1;
    } else if (diff_line[0] == ' ') {
      // Context line (unchanged)
      NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
      strncpy(file_line->line, diff_line, sizeof(file_line->line) - 1);
      file_line->line[sizeof(file_line->line) - 1] = '\0';
      file_line->type = ' ';
      file_line->is_diff_line = 0;
      viewer->file_line_count++;
    }
  }

  pclose(diff_fp);

  // If no changes were found, show a message
  if (!found_changes) {
    NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
    strncpy(file_line->line, "No changes in this file",
            sizeof(file_line->line) - 1);
    file_line->line[sizeof(file_line->line) - 1] = '\0';
    file_line->type = ' ';
    file_line->is_diff_line = 0;
    viewer->file_line_count++;
  }

  return viewer->file_line_count;
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

  // Save current screen
  WINDOW *saved_screen = dupwin(stdscr);

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
    if (saved_screen)
      delwin(saved_screen);
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

  // Clean up windows
  delwin(title_win);
  delwin(message_win);

  // Restore the screen
  if (saved_screen) {
    overwrite(saved_screen, stdscr);
    delwin(saved_screen);
  }

  // Force a complete redraw
  clear();
  refresh();

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

  // Commit with the provided title and message
  char commit_cmd[2048];
  if (commit_message && strlen(commit_message) > 0) {
    snprintf(commit_cmd, sizeof(commit_cmd),
             "git commit -m \"%s\" -m \"%s\" 2>/dev/null >/dev/null",
             commit_title, commit_message);
  } else {
    snprintf(commit_cmd, sizeof(commit_cmd),
             "git commit -m \"%s\" 2>/dev/null >/dev/null", commit_title);
  }
  int result = system(commit_cmd);

  if (result == 0) {
    // Small delay to ensure git has processed the commit
    usleep(100000); // 100ms delay

    // Refresh file list and commit history
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

    // Reload the currently selected file's diff if files still exist
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_full_file_with_diff(viewer,
                               viewer->files[viewer->selected_file].filename);
    }

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
      load_full_file_with_diff(viewer,
                               viewer->files[viewer->selected_file].filename);
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
      load_full_file_with_diff(viewer,
                               viewer->files[viewer->selected_file].filename);
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

    // Amend the commit with new message
    char amend_cmd[2048];
    if (strlen(new_message) > 0) {
      snprintf(amend_cmd, sizeof(amend_cmd),
               "git commit --amend -m \"%s\" -m \"%s\" 2>/dev/null >/dev/null",
               new_title, new_message);
    } else {
      snprintf(amend_cmd, sizeof(amend_cmd),
               "git commit --amend -m \"%s\" 2>/dev/null >/dev/null",
               new_title);
    }

    int result = system(amend_cmd);

    if (result == 0) {
      // Small delay to ensure git has processed the amend
      usleep(100000); // 100ms delay

      // Refresh everything
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

      // Reload current file if any files exist
      if (viewer->file_count > 0 &&
          viewer->selected_file < viewer->file_count) {
        load_full_file_with_diff(viewer,
                                 viewer->files[viewer->selected_file].filename);
      }

      return 1;
    }
  }

  return 0;
}

/**
 * Push specific commit
 */
int push_commit(NCursesDiffViewer *viewer, int commit_index) {
  if (!viewer || commit_index < 0 || commit_index >= viewer->commit_count)
    return 0;

  // Check for branch divergence first
  int commits_ahead = 0;
  int commits_behind = 0;
  int is_diverged = check_branch_divergence(&commits_ahead, &commits_behind);

  // If diverged, show confirmation dialog
  if (is_diverged) {
    if (!show_diverged_branch_dialog(commits_ahead, commits_behind)) {
      // User cancelled
      return 0;
    }
  }

  // Start pushing animation immediately
  viewer->sync_status = SYNC_STATUS_PUSHING_APPEARING;
  viewer->animation_frame = 0;
  viewer->text_char_count = 0;

  // Force render and refresh to show immediate animation start
  render_status_bar(viewer);
  wrefresh(viewer->status_bar_win);
  refresh();

  // Do the actual push work
  int result;
  if (is_diverged) {
    // Use force push with lease for safety
    result =
        system("git push --force-with-lease origin 2>/dev/null >/dev/null");
  } else {
    // Normal push
    result = system("git push origin 2>/dev/null >/dev/null");
  }

  if (result == 0) {
    // Immediately transition to "Pushed!" animation
    viewer->sync_status = SYNC_STATUS_PUSHED_APPEARING;
    viewer->animation_frame = 0;
    viewer->text_char_count = 0;

    // Refresh commit history to get proper push status
    get_commit_history(viewer);
    return 1;
  } else {
    // Push failed, reset to idle
    viewer->sync_status = SYNC_STATUS_IDLE;
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
      load_full_file_with_diff(viewer,
                               viewer->files[viewer->selected_file].filename);
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

    // Show selection indicator
    if (i == viewer->selected_file) {
      if (viewer->current_mode == NCURSES_MODE_FILE_LIST) {
        wattron(viewer->file_list_win, COLOR_PAIR(5));
        mvwprintw(viewer->file_list_win, y, 1, ">");
      } else {
        wattron(viewer->file_list_win, COLOR_PAIR(1));
        mvwprintw(viewer->file_list_win, y, 1, "*");
      }
    } else {
      mvwprintw(viewer->file_list_win, y, 1, " ");
    }

    // Status indicator
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

    // Color filename green if marked for commit
    if (viewer->files[i].marked_for_commit) {
      wattron(viewer->file_list_win, COLOR_PAIR(1));
    }
    mvwprintw(viewer->file_list_win, y, 4, "%s", truncated_name);
    if (viewer->files[i].marked_for_commit) {
      wattroff(viewer->file_list_win, COLOR_PAIR(1));
    }

    if (i == viewer->selected_file) {
      if (viewer->current_mode == NCURSES_MODE_FILE_LIST) {
        wattroff(viewer->file_list_win, COLOR_PAIR(5));
      } else {
        wattroff(viewer->file_list_win, COLOR_PAIR(1));
      }
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
  mvwprintw(viewer->commit_list_win, 0, 2, " 3. Commits ");

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

    // Show selection indicator for commit list mode
    if (i == viewer->selected_commit &&
        viewer->current_mode == NCURSES_MODE_COMMIT_LIST) {
      wattron(viewer->commit_list_win, COLOR_PAIR(5));
      mvwprintw(viewer->commit_list_win, y, 1, ">");
      wattroff(viewer->commit_list_win, COLOR_PAIR(5));
    } else {
      mvwprintw(viewer->commit_list_win, y, 1, " ");
    }

    // Show commit hash with color based on push status
    if (viewer->commits[i].is_pushed) {
      wattron(viewer->commit_list_win, COLOR_PAIR(1)); // Green for pushed
    } else {
      wattron(viewer->commit_list_win, COLOR_PAIR(2)); // Red for unpushed
    }
    mvwprintw(viewer->commit_list_win, y, 2, "%s", viewer->commits[i].hash);
    if (viewer->commits[i].is_pushed) {
      wattroff(viewer->commit_list_win, COLOR_PAIR(1));
    } else {
      wattroff(viewer->commit_list_win, COLOR_PAIR(2));
    }

    // Show author initials with same color as hash
    if (viewer->commits[i].is_pushed) {
      wattron(viewer->commit_list_win, COLOR_PAIR(1)); // Green for pushed
    } else {
      wattron(viewer->commit_list_win, COLOR_PAIR(2)); // Red for unpushed
    }
    mvwprintw(viewer->commit_list_win, y, 10, "%s",
              viewer->commits[i].author_initials);
    if (viewer->commits[i].is_pushed) {
      wattroff(viewer->commit_list_win, COLOR_PAIR(1));
    } else {
      wattroff(viewer->commit_list_win, COLOR_PAIR(2));
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
  }

  wrefresh(viewer->commit_list_win);
}

/**
 * Render the file content window
 */
void render_file_content_window(NCursesDiffViewer *viewer) {
  if (!viewer || !viewer->file_content_win)
    return;

  // Clear the window content area (but preserve borders)
  int height, width;
  getmaxyx(viewer->file_content_win, height, width);
  for (int y = 1; y < height - 1; y++) {
    wmove(viewer->file_content_win, y, 1);
    for (int x = 1; x < width - 1; x++) {
      waddch(viewer->file_content_win, ' ');
    }
  }

  // Draw rounded border
  draw_rounded_box(viewer->file_content_win);

  if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
    // Show file content (preview in list mode, scrollable in view mode)
    if (viewer->current_mode == NCURSES_MODE_FILE_LIST) {
      mvwprintw(viewer->file_content_win, 0, 2, " 2. %s (Preview) ",
                viewer->files[viewer->selected_file].filename);
    } else {
      mvwprintw(viewer->file_content_win, 0, 2, " 2. %s (Scrollable) ",
                viewer->files[viewer->selected_file].filename);
    }

    int max_lines_visible = viewer->terminal_height - 4;
    int content_width = viewer->terminal_width - viewer->file_panel_width - 3;

    for (int i = 0; i < max_lines_visible &&
                    (i + viewer->file_scroll_offset) < viewer->file_line_count;
         i++) {

      int line_idx = i + viewer->file_scroll_offset;
      NCursesFileLine *line = &viewer->file_lines[line_idx];

      int y = i + 1;

      // Apply color based on line type
      if (line->type == '+') {
        wattron(viewer->file_content_win, COLOR_PAIR(1)); // Green for additions
      } else if (line->type == '-') {
        wattron(viewer->file_content_win, COLOR_PAIR(2)); // Red for deletions
      } else if (line->type == '@') {
        wattron(viewer->file_content_win,
                COLOR_PAIR(3)); // Cyan for hunk headers
      }

      // Truncate line to fit window
      char display_line[1024];
      if ((int)strlen(line->line) > content_width - 2) {
        strncpy(display_line, line->line, content_width - 5);
        display_line[content_width - 5] = '\0';
        strcat(display_line, "...");
      } else {
        strcpy(display_line, line->line);
      }

      mvwprintw(viewer->file_content_win, y, 1, "%s", display_line);

      // Reset color
      if (line->type == '+') {
        wattroff(viewer->file_content_win, COLOR_PAIR(1));
      } else if (line->type == '-') {
        wattroff(viewer->file_content_win, COLOR_PAIR(2));
      } else if (line->type == '@') {
        wattroff(viewer->file_content_win, COLOR_PAIR(3));
      }
    }

    // Show scroll indicator
    if (viewer->file_line_count > max_lines_visible) {
      int scroll_pos = (viewer->file_scroll_offset * max_lines_visible) /
                       viewer->file_line_count;
      mvwprintw(viewer->file_content_win, scroll_pos + 1, content_width + 1,
                "█");
    }

    // Show current position info only in file view mode
    if (viewer->current_mode == NCURSES_MODE_FILE_VIEW) {
      mvwprintw(viewer->file_content_win, max_lines_visible + 1, 1,
                "Line %d-%d of %d", viewer->file_scroll_offset + 1,
                viewer->file_scroll_offset + max_lines_visible <
                        viewer->file_line_count
                    ? viewer->file_scroll_offset + max_lines_visible
                    : viewer->file_line_count,
                viewer->file_line_count);
    } else {
      mvwprintw(viewer->file_content_win, max_lines_visible + 1, 1,
                "Press Enter to enable scrolling");
    }
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
  } else if (viewer->current_mode == NCURSES_MODE_FILE_VIEW) {
    strcpy(keybindings, "Scroll: j/k | Page: Ctrl+U/D | Back: Esc");
  }

  mvwprintw(viewer->status_bar_win, 0, 1, "%s", keybindings);

  // Right side: Sync status
  char sync_text[64] = "";
  char *spinner_chars[] = {"|", "/", "-", "\\"};
  int spinner_idx =
      viewer->spinner_frame % 4; // Change every frame (~50ms per character)

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
  if (current_time - viewer->last_sync_time >= 30) {
    viewer->sync_status = SYNC_STATUS_SYNCING_APPEARING;
    viewer->last_sync_time = current_time;
    viewer->animation_frame = 0;
    viewer->text_char_count = 0;

    // Do the actual sync work immediately
    get_ncurses_changed_files(viewer);
    if (viewer->selected_file < viewer->file_count && viewer->file_count > 0) {
      load_full_file_with_diff(viewer,
                               viewer->files[viewer->selected_file].filename);
    }
    get_commit_history(viewer);
  }

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
        // Visible with spinner for 1.2 seconds (60 frames at 20ms) - faster
        if (viewer->animation_frame >= 60) {
          viewer->sync_status = SYNC_STATUS_PUSHING_DISAPPEARING;
          viewer->animation_frame = 0;
          viewer->text_char_count = 7;
        }
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
}

/**
 * Handle keyboard input for navigation
 */
int handle_ncurses_diff_input(NCursesDiffViewer *viewer, int key) {
  if (!viewer)
    return 0;

  int max_lines_visible = viewer->terminal_height - 4;

  // Global quit commands
  if (key == 'q' || key == 'Q') {
    return 0; // Exit
  }

  // Global number key navigation
  switch (key) {
  case '1':
    viewer->current_mode = NCURSES_MODE_FILE_LIST;
    break;
  case '2':
    // Switch to file view mode and load selected file
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
      load_full_file_with_diff(viewer,
                               viewer->files[viewer->selected_file].filename);
      viewer->current_mode = NCURSES_MODE_FILE_VIEW;
    }
    break;
  case '3':
    viewer->current_mode = NCURSES_MODE_COMMIT_LIST;
    break;
  case '4':
    viewer->current_mode = NCURSES_MODE_STASH_LIST;
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
        // Auto-preview the selected file
        if (viewer->file_count > 0) {
          load_full_file_with_diff(
              viewer, viewer->files[viewer->selected_file].filename);
        }
      }
      break;

    case KEY_DOWN:
    case 'j':
      if (viewer->selected_file < viewer->file_count - 1) {
        viewer->selected_file++;
        // Auto-preview the selected file
        if (viewer->file_count > 0) {
          load_full_file_with_diff(
              viewer, viewer->files[viewer->selected_file].filename);
        }
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
			create_ncurses_git_stash(viewer);
			break;

    case 'c':
    case 'C': // Commit marked files
    {
      char commit_title[MAX_COMMIT_TITLE_LEN];
      char commit_message[2048];
      if (get_commit_title_input(commit_title, MAX_COMMIT_TITLE_LEN,
                                 commit_message, sizeof(commit_message))) {
        commit_marked_files(viewer, commit_title, commit_message);
      }
    } break;

    case '\t': // Tab - switch to commit list mode
      viewer->current_mode = NCURSES_MODE_COMMIT_LIST;
      break;

    case '\n':
    case '\r':
    case KEY_ENTER:
      // Enter file view mode and load the selected file
      if (viewer->file_count > 0 &&
          viewer->selected_file < viewer->file_count) {
        load_full_file_with_diff(viewer,
                                 viewer->files[viewer->selected_file].filename);
        viewer->current_mode = NCURSES_MODE_FILE_VIEW;
      }
      break;
    }
  } else if (viewer->current_mode == NCURSES_MODE_FILE_VIEW) {
    // File view mode navigation
    switch (key) {
    case 27:                                         // ESC
      viewer->current_mode = NCURSES_MODE_FILE_LIST; // Return to file list mode
      break;

    case KEY_UP:
    case 'k':
      // Scroll content up
      if (viewer->file_scroll_offset > 0) {
        viewer->file_scroll_offset--;
      }
      break;

    case KEY_DOWN:
    case 'j':
      // Scroll content down
      if (viewer->file_line_count > max_lines_visible &&
          viewer->file_scroll_offset <
              viewer->file_line_count - max_lines_visible) {
        viewer->file_scroll_offset++;
      }
      break;

    case 21: // Ctrl+U
      // Scroll up 30 lines
      viewer->file_scroll_offset -= 30;
      if (viewer->file_scroll_offset < 0) {
        viewer->file_scroll_offset = 0;
      }
      break;

    case 4: // Ctrl+D
      // Scroll down 30 lines
      if (viewer->file_line_count > max_lines_visible) {
        viewer->file_scroll_offset += 30;
        if (viewer->file_scroll_offset >
            viewer->file_line_count - max_lines_visible) {
          viewer->file_scroll_offset =
              viewer->file_line_count - max_lines_visible;
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
  } else if (viewer->current_mode == NCURSES_MODE_COMMIT_LIST) {
    // Commit list mode navigation
    switch (key) {
    case 27:   // ESC
    case '\t': // Tab - return to file list mode
      viewer->current_mode = NCURSES_MODE_FILE_LIST;
      break;

    case KEY_UP:
    case 'k':
      if (viewer->selected_commit > 0) {
        viewer->selected_commit--;
      }
      break;

    case KEY_DOWN:
    case 'j':
      if (viewer->selected_commit < viewer->commit_count - 1) {
        viewer->selected_commit++;
      }
      break;

    case 'P': // Push commit
      if (viewer->commit_count > 0 &&
          viewer->selected_commit < viewer->commit_count) {
        push_commit(viewer, viewer->selected_commit);
      }
      break;

    case 'p': // Pull commits
      pull_commits(viewer);
      break;

    case 'r': // Reset (soft) - undo commit but keep changes
      if (viewer->commit_count > 0 && viewer->selected_commit == 0) {
        reset_commit_soft(viewer, viewer->selected_commit);
      }
      break;

    case 'R': // Reset (hard) - undo commit and discard changes
      if (viewer->commit_count > 0 && viewer->selected_commit == 0) {
        reset_commit_hard(viewer, viewer->selected_commit);
      }
      break;

    case 'a':
    case 'A': // Amend most recent commit
      if (viewer->commit_count > 0) {
        amend_commit(viewer);
      }
      break;
    }
  } else if (viewer->current_mode == NCURSES_MODE_STASH_LIST) {
    // Stash list mode navigation
    switch (key) {
    case 27:   // ESC
    case '\t': // Tab - return to file list mode
      viewer->current_mode = NCURSES_MODE_FILE_LIST;
      break;

    case KEY_UP:
    case 'k':
      if (viewer->selected_stash > 0) {
        viewer->selected_stash--;
      }
      break;

    case KEY_DOWN:
    case 'j':
      if (viewer->selected_stash < viewer->stash_count - 1) {
        viewer->selected_stash++;
      }
      break;

    case ' ': // Space - Apply stash (keeps stash in list)
      if (viewer->stash_count > 0 && viewer->selected_stash < viewer->stash_count) {
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
          if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
            load_full_file_with_diff(viewer, viewer->files[viewer->selected_file].filename);
          }
        }
      }
      break;

    case 'g':
    case 'G': // Pop stash (applies and removes from list)
      if (viewer->stash_count > 0 && viewer->selected_stash < viewer->stash_count) {
        if (pop_git_stash(viewer->selected_stash)) {
          // Refresh everything after popping stash
          get_ncurses_changed_files(viewer);
          get_ncurses_git_stashes(viewer);
          get_commit_history(viewer);
          
          // Adjust selected stash if needed
          if (viewer->selected_stash >= viewer->stash_count && viewer->stash_count > 0) {
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
          if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
            load_full_file_with_diff(viewer, viewer->files[viewer->selected_file].filename);
          }
        }
      }
      break;

    case 'd':
    case 'D': // Drop stash (removes without applying)
      if (viewer->stash_count > 0 && viewer->selected_stash < viewer->stash_count) {
        if (drop_git_stash(viewer->selected_stash)) {
          // Refresh stash list after dropping
          get_ncurses_git_stashes(viewer);
          
          // Adjust selected stash if needed
          if (viewer->selected_stash >= viewer->stash_count && viewer->stash_count > 0) {
            viewer->selected_stash = viewer->stash_count - 1;
          }
        }
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

  // Get changed files (can be 0, that's okay)
  get_ncurses_changed_files(&viewer);


	//get stashes
	get_ncurses_git_stashes(&viewer);

  // Load commit history
  get_commit_history(&viewer);

  // Load the first file by default
  if (viewer.file_count > 0) {
    load_full_file_with_diff(&viewer, viewer.files[0].filename);
  }

  // Initial display
  attron(COLOR_PAIR(3));
  if (viewer.current_mode == NCURSES_MODE_FILE_LIST) {
    mvprintw(0, 0,
             "Git Diff Viewer: 1=files 2=view 3=commits 4=stashes | j/k=nav Space=mark "
             "A=all S=stash C=commit P=push | q=quit");
  } else if (viewer.current_mode == NCURSES_MODE_FILE_VIEW) {
    mvprintw(0, 0,
             "Git Diff Viewer: 1=files 2=view 3=commits 4=stashes | j/k=scroll "
             "Ctrl+U/D=30lines | q=quit");
  } else {
    mvprintw(0, 0,
             "Git Diff Viewer: 1=files 2=view 3=commits 4=stashes | j/k=nav P=push "
             "p=pull r/R=reset a=amend | q=quit");
  }
  attroff(COLOR_PAIR(3));
  refresh();

  render_file_list_window(&viewer);
  render_file_content_window(&viewer);
  render_commit_list_window(&viewer);
	render_stash_list_window(&viewer);
  render_status_bar(&viewer);

  // Main display loop
  int running = 1;
  NCursesViewMode last_mode = viewer.current_mode;

  while (running) {
    // Only update title if mode changed
    if (viewer.current_mode != last_mode) {
      // Clear just the title line
      move(0, 0);
      clrtoeol();

      attron(COLOR_PAIR(3));
      if (viewer.current_mode == NCURSES_MODE_FILE_LIST) {
        mvprintw(0, 0,
                 "Git Diff Viewer: 1=files 2=view 3=commits 4=stashes | j/k=nav "
                 "Space=mark A=all S=stash C=commit P=push | q=quit");
      } else if (viewer.current_mode == NCURSES_MODE_FILE_VIEW) {
        mvprintw(0, 0,
                 "Git Diff Viewer: 1=files 2=view 3=commits 4=stashes | j/k=scroll "
                 "Ctrl+U/D=30lines | q=quit");
      } else {
        mvprintw(0, 0,
                 "Git Diff Viewer: 1=files 2=view 3=commits 4=stashes | j/k=nav P=push "
                 "p=pull r/R=reset a=amend | q=quit");
      }
      attroff(COLOR_PAIR(3));
      refresh();
      last_mode = viewer.current_mode;
    }

    // Update sync status and check for new files
    update_sync_status(&viewer);

    render_file_list_window(&viewer);
    render_file_content_window(&viewer);
    render_commit_list_window(&viewer);
		render_stash_list_window(&viewer);
    render_status_bar(&viewer);

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
int get_ncurses_git_stashes(NCursesDiffViewer *viewer) {
  if (!viewer) return 0;
  
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
    mvwprintw(input_win, 0, 2, " Enter stash name (ESC to cancel, Enter to confirm) ");
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
  if (!viewer) return 0;
  
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
      load_full_file_with_diff(viewer, viewer->files[viewer->selected_file].filename);
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
  mvwprintw(viewer->stash_list_win, 0, 2, " 4. Stashes ");

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

      // Show selection indicator for stash list mode
      if (i == viewer->selected_stash &&
          viewer->current_mode == NCURSES_MODE_STASH_LIST) {
        wattron(viewer->stash_list_win, COLOR_PAIR(5));
        mvwprintw(viewer->stash_list_win, y, 1, ">");
        wattroff(viewer->stash_list_win, COLOR_PAIR(5));
      } else {
        mvwprintw(viewer->stash_list_win, y, 1, " ");
      }

      // Show stash info (truncated to fit panel)
      int max_stash_len = viewer->file_panel_width - 4;
      char truncated_stash[256];
      if ((int)strlen(viewer->stashes[i].stash_info) > max_stash_len) {
        strncpy(truncated_stash, viewer->stashes[i].stash_info, max_stash_len - 2);
        truncated_stash[max_stash_len - 2] = '\0';
        strcat(truncated_stash, "..");
      } else {
        strcpy(truncated_stash, viewer->stashes[i].stash_info);
      }

      // Color stash entries yellow
      wattron(viewer->stash_list_win, COLOR_PAIR(4));
      mvwprintw(viewer->stash_list_win, y, 2, "%s", truncated_stash);
      wattroff(viewer->stash_list_win, COLOR_PAIR(4));
    }
  }

  wrefresh(viewer->stash_list_win);
}

/**
 * Clean up ncurses diff viewer resources
 */
void cleanup_ncurses_diff_viewer(NCursesDiffViewer *viewer) {
  if (viewer) {
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
    if (viewer->status_bar_win) {
      delwin(viewer->status_bar_win);
    }
  }
  endwin();
}

