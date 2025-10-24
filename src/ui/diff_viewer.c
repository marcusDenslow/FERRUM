// thisis a test

#include "diff_viewer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int init_diff_viewer(DiffViewer *viewer) {
  if (!viewer)
    return 0;

  memset(viewer, 0, sizeof(DiffViewer));
  viewer->selected_file = 0;
  viewer->diff_scroll_offset = 0;
  viewer->current_mode = MODE_FILE_LIST; // Start in file list mode

  get_terminal_size(&viewer->terminal_width, &viewer->terminal_height);
  viewer->file_panel_width =
      viewer->terminal_width * 0.3; // 30% of screen width

  return 1;
}

void get_terminal_size(int *width, int *height) {
  struct winsize w;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
  *width = w.ws_col;
  *height = w.ws_row;
}

int get_changed_files(DiffViewer *viewer) {
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
    strncpy(viewer->files[viewer->file_count].filename, filename,
            MAX_FILENAME_LEN - 1);
    viewer->files[viewer->file_count].filename[MAX_FILENAME_LEN - 1] = '\0';

    viewer->file_count++;
  }

  pclose(fp);
  return viewer->file_count;
}

int is_new_file(const char *filename) {
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

int load_new_file_content(DiffViewer *viewer, const char *filename) {
  if (!viewer || !filename)
    return 0;

  FILE *fp = fopen(filename, "r");
  if (!fp)
    return 0;

  viewer->diff_line_count = 0;
  viewer->diff_scroll_offset = 0;

  // Add header line
  DiffLine *header = &viewer->diff_lines[viewer->diff_line_count++];
  snprintf(header->line, sizeof(header->line), "@@ New file: %s @@", filename);
  header->type = '@';
  header->line_number_old = 0;
  header->line_number_new = 0;

  char line[1024];
  int line_number = 1;

  while (fgets(line, sizeof(line), fp) != NULL &&
         viewer->diff_line_count < MAX_DIFF_LINES) {
    // Remove newline
    char *newline_pos = strchr(line, '\n');
    if (newline_pos)
      *newline_pos = '\0';

    DiffLine *diff_line = &viewer->diff_lines[viewer->diff_line_count];

    // Format as addition
    snprintf(diff_line->line, sizeof(diff_line->line), "+%s", line);
    diff_line->type = '+';
    diff_line->line_number_old = 0;
    diff_line->line_number_new = line_number++;

    viewer->diff_line_count++;
  }

  fclose(fp);
  return viewer->diff_line_count;
}

int load_file_diff(DiffViewer *viewer, const char *filename) {
  if (!viewer || !filename)
    return 0;

  // Check if this is a new untracked file
  if (is_new_file(filename)) {
    return load_new_file_content(viewer, filename);
  }

  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "git diff HEAD -- \"%s\" 2>/dev/null", filename);

  FILE *fp = popen(cmd, "r");
  if (!fp)
    return 0;

  viewer->diff_line_count = 0;
  viewer->diff_scroll_offset = 0;

  char line[1024];
  int old_line = 0, new_line = 0;

  while (fgets(line, sizeof(line), fp) != NULL &&
         viewer->diff_line_count < MAX_DIFF_LINES) {
    // Remove newline
    char *newline_pos = strchr(line, '\n');
    if (newline_pos)
      *newline_pos = '\0';

    DiffLine *diff_line = &viewer->diff_lines[viewer->diff_line_count];

    // Parse diff line
    if (line[0] == '@' && line[1] == '@') {
      // Hunk header: @@-old_start,old_count +new_start,new_count@@
      sscanf(line, "@@-%d,%*d +%d,%*d@@", &old_line, &new_line);
      old_line--; // Will be incremented before use
      new_line--;

      strncpy(diff_line->line, line, sizeof(diff_line->line) - 1);
      diff_line->line[sizeof(diff_line->line) - 1] = '\0';
      diff_line->type = '@';
      diff_line->line_number_old = old_line;
      diff_line->line_number_new = new_line;
    } else if (line[0] == '+') {
      new_line++;
      strncpy(diff_line->line, line, sizeof(diff_line->line) - 1);
      diff_line->line[sizeof(diff_line->line) - 1] = '\0';
      diff_line->type = '+';
      diff_line->line_number_old = old_line;
      diff_line->line_number_new = new_line;
    } else if (line[0] == '-') {
      old_line++;
      strncpy(diff_line->line, line, sizeof(diff_line->line) - 1);
      diff_line->line[sizeof(diff_line->line) - 1] = '\0';
      diff_line->type = '-';
      diff_line->line_number_old = old_line;
      diff_line->line_number_new = new_line;
    } else if (line[0] == ' ') {
      old_line++;
      new_line++;
      strncpy(diff_line->line, line, sizeof(diff_line->line) - 1);
      diff_line->line[sizeof(diff_line->line) - 1] = '\0';
      diff_line->type = ' ';
      diff_line->line_number_old = old_line;
      diff_line->line_number_new = new_line;
    } else {
      // Skip other lines (file headers, etc.)
      continue;
    }

    viewer->diff_line_count++;
  }

  pclose(fp);

  // If no diff content but file exists, it might be a new file added to git
  if (viewer->diff_line_count == 0) {
    return load_new_file_content(viewer, filename);
  }

  return viewer->diff_line_count;
}

void render_diff_viewer(DiffViewer *viewer) {
  if (!viewer)
    return;

  // Clear screen
  printf("\033[2J\033[H");

  // Title bar
  printf(ANSI_COLOR_CYAN "Git Diff Viewer" ANSI_COLOR_RESET);
  if (viewer->current_mode == MODE_FILE_LIST) {
    printf(" - Use ↑/↓ to navigate files, Enter to view, q to quit\n");
  } else {
    printf(" - Use ↑/↓ to scroll, ESC to return to file list, q to quit\n");
  }
  printf("─%.0*s\n", viewer->terminal_width - 1,
         "─────────────────────────────────────────────────────────────────────"
         "───────────");

  int start_row = 2;
  int available_height = viewer->terminal_height - start_row - 1;

  // Render file list (left panel)
  for (int i = 0; i < available_height && i < viewer->file_count; i++) {
    printf("\033[%d;1H", start_row + i + 1); // Position cursor

    if (i == viewer->selected_file) {
      if (viewer->current_mode == MODE_FILE_LIST) {
        printf(ANSI_COLOR_CYAN "► " ANSI_COLOR_RESET);
      } else {
        printf(ANSI_COLOR_GREEN "► " ANSI_COLOR_RESET);
      }
    } else {
      printf("  ");
    }

    // Status indicator
    char status = viewer->files[i].status;
    if (status == 'M') {
      printf(ANSI_COLOR_YELLOW "M" ANSI_COLOR_RESET);
    } else if (status == 'A') {
      printf(ANSI_COLOR_GREEN "A" ANSI_COLOR_RESET);
    } else if (status == 'D') {
      printf(ANSI_COLOR_RED "D" ANSI_COLOR_RESET);
    } else {
      printf("%c", status);
    }

    printf(" ");

    // Filename (truncated to fit panel)
    char truncated_name[256];
    int max_name_len = viewer->file_panel_width - 6;
    if ((int)strlen(viewer->files[i].filename) > max_name_len) {
      strncpy(truncated_name, viewer->files[i].filename, max_name_len - 3);
      truncated_name[max_name_len - 3] = '\0';
      strcat(truncated_name, "...");
    } else {
      strcpy(truncated_name, viewer->files[i].filename);
    }

    printf("%-*s", max_name_len, truncated_name);

    // Vertical separator
    printf("│");
  }

  // Render diff content (right panel)
  if (viewer->current_mode == MODE_FILE_CONTENT && viewer->file_count > 0 &&
      viewer->selected_file < viewer->file_count) {
    int diff_panel_start = viewer->file_panel_width + 1;
    int diff_panel_width = viewer->terminal_width - diff_panel_start;

    for (int i = 0; i < available_height &&
                    (i + viewer->diff_scroll_offset) < viewer->diff_line_count;
         i++) {

      int line_idx = i + viewer->diff_scroll_offset;
      DiffLine *line = &viewer->diff_lines[line_idx];

      printf("\033[%d;%dH", start_row + i + 1, diff_panel_start + 1);

      // Color code based on line type
      if (line->type == '+') {
        printf(ANSI_COLOR_GREEN);
      } else if (line->type == '-') {
        printf(ANSI_COLOR_RED);
      } else if (line->type == '@') {
        printf(ANSI_COLOR_CYAN);
      }

      // Truncate line to fit panel
      char display_line[1024];
      int max_len = diff_panel_width - 1;
      if ((int)strlen(line->line) > max_len) {
        strncpy(display_line, line->line, max_len - 3);
        display_line[max_len - 3] = '\0';
        strcat(display_line, "...");
      } else {
        strcpy(display_line, line->line);
      }

      printf("%-*s", max_len, display_line);
      printf(ANSI_COLOR_RESET);
    }
  } else if (viewer->current_mode == MODE_FILE_LIST) {
    // Show help text in the right panel when in file list mode
    int diff_panel_start = viewer->file_panel_width + 1;
    printf("\033[%d;%dH", start_row + 2, diff_panel_start + 3);
    printf(ANSI_COLOR_YELLOW
           "Select a file and press Enter to view its diff" ANSI_COLOR_RESET);
  }

  // Status line at bottom
  printf("\033[%d;1H", viewer->terminal_height);
  if (viewer->file_count > 0) {
    if (viewer->current_mode == MODE_FILE_LIST) {
      printf("File %d/%d: %s [File List Mode]", viewer->selected_file + 1,
             viewer->file_count, viewer->files[viewer->selected_file].filename);
    } else {
      printf("File %d/%d: %s [Content Mode - Line %d/%d]",
             viewer->selected_file + 1, viewer->file_count,
             viewer->files[viewer->selected_file].filename,
             viewer->diff_scroll_offset + 1,
             viewer->diff_line_count > 0 ? viewer->diff_line_count : 1);
    }
  } else {
    printf("No changed files found");
  }

  fflush(stdout);
}

int handle_diff_input(DiffViewer *viewer, char key) {
  if (!viewer)
    return 0;

  int available_height = viewer->terminal_height - 3;

  switch (key) {
  case 'q':
  case 'Q':
    return 0; // Exit

  case 27: // ESC
    if (viewer->current_mode == MODE_FILE_CONTENT) {
      viewer->current_mode = MODE_FILE_LIST;
    } else {
      return 0; // Exit if already in file list mode
    }
    break;

  case 'k':
  case 65: // Up arrow
    if (viewer->current_mode == MODE_FILE_LIST) {
      // Navigate file list
      if (viewer->selected_file > 0) {
        viewer->selected_file--;
      }
    } else {
      // Scroll diff content up
      if (viewer->diff_scroll_offset > 0) {
        viewer->diff_scroll_offset--;
      }
    }
    break;

  case 'j':
  case 66: // Down arrow
    if (viewer->current_mode == MODE_FILE_LIST) {
      // Navigate file list
      if (viewer->selected_file < viewer->file_count - 1) {
        viewer->selected_file++;
      }
    } else {
      // Scroll diff content down
      if (viewer->diff_line_count > available_height &&
          viewer->diff_scroll_offset <
              viewer->diff_line_count - available_height) {
        viewer->diff_scroll_offset++;
      }
    }
    break;

  case '\n':
  case '\r':
    if (viewer->current_mode == MODE_FILE_LIST && viewer->file_count > 0) {
      // Enter file content mode and load diff
      viewer->current_mode = MODE_FILE_CONTENT;
      viewer->diff_scroll_offset = 0; // Reset scroll position
      load_file_diff(viewer, viewer->files[viewer->selected_file].filename);
    }
    break;
  }

  return 1; // Continue
}

void set_raw_mode(struct termios *orig_termios) {
  tcgetattr(STDIN_FILENO, orig_termios);

  struct termios raw = *orig_termios;
  raw.c_lflag &= ~(ICANON | ECHO);
  raw.c_cc[VTIME] = 0;
  raw.c_cc[VMIN] = 1;

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void restore_terminal_mode(struct termios *orig_termios) {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, orig_termios);
}

int run_diff_viewer(void) {
  DiffViewer viewer;
  struct termios orig_termios;

  if (!init_diff_viewer(&viewer)) {
    printf("Failed to initialize diff viewer\n");
    return 1;
  }

  if (get_changed_files(&viewer) == 0) {
    printf("No changed files found\n");
    return 1;
  }

  // Don't load initial file diff - start in file list mode

  set_raw_mode(&orig_termios);

  int running = 1;
  while (running) {
    render_diff_viewer(&viewer);

    char c = getchar();

    // Handle escape sequences for arrow keys
    if (c == 27) {
      char seq[3];
      if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
        if (read(STDIN_FILENO, &seq[1], 1) == 1) {
          running = handle_diff_input(&viewer, seq[1]);
        }
      } else {
        running = handle_diff_input(&viewer, c);
      }
    } else {
      running = handle_diff_input(&viewer, c);
    }
  }

  restore_terminal_mode(&orig_termios);
  printf("\033[2J\033[H"); // Clear screen

  cleanup_diff_viewer(&viewer);
  return 0;
}

void cleanup_diff_viewer(DiffViewer *viewer) {
  // Nothing to clean up for now, but good to have for future expansion
  (void)viewer;
}
