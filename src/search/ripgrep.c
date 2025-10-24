
#include "ripgrep.h"
#include "common.h"
#include "line_reader.h"
#include "shell.h" // Add this for lsh_execute
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>  // For PATH_MAX
#include <unistd.h>  // For access function
#include <termios.h> // For terminal control

int is_rg_installed(void) {
  // Try to run rg --version to check if it's installed
  FILE *fp = popen("rg --version 2>/dev/null", "r");
  if (fp == NULL) {
    return 0;
  }

  // Read output (if any)
  char buffer[128];
  int has_output = (fgets(buffer, sizeof(buffer), fp) != NULL);

  // Close the process
  pclose(fp);

  return has_output;
}

void show_rg_install_instructions(void) {
  printf("\nripgrep (rg) is not installed on this system. To use this feature, "
         "install ripgrep:\n\n");
  printf("Installation options:\n");
  printf("1. Using package manager (Debian/Ubuntu):\n");
  printf("   sudo apt install ripgrep\n\n");
  printf("2. Using package manager (Fedora):\n");
  printf("   sudo dnf install ripgrep\n\n");
  printf("3. Using package manager (Arch Linux):\n");
  printf("   sudo pacman -S ripgrep\n\n");
  printf("4. Download prebuilt binary from: "
         "https://github.com/BurntSushi/ripgrep/releases\n\n");
  printf("After installation, restart your shell.\n");
}

int is_editor_available_for_rg(const char *editor) {
  char command[256];
  snprintf(command, sizeof(command), "%s --version >/dev/null 2>&1", editor);
  return (system(command) == 0);
}

int rg_open_in_editor(const char *file_path, int line_number) {
  char command[2048] = {0};
  int success = 0;

  // Try to detect available editors (in order of preference)
  if (is_editor_available_for_rg("nvim")) {
    // Neovim is available - construct command with +line_number
    snprintf(command, sizeof(command), "nvim +%d \"%s\"", line_number,
             file_path);
    success = 1;
  } else if (is_editor_available_for_rg("vim")) {
    // Vim is available
    snprintf(command, sizeof(command), "vim +%d \"%s\"", line_number,
             file_path);
    success = 1;
  } else if (is_editor_available_for_rg("nano")) {
    // Try nano if available
    // Note: nano doesn't have perfect line number navigation
    snprintf(command, sizeof(command), "nano +%d \"%s\"", line_number,
             file_path);
    success = 1;
  } else if (is_editor_available_for_rg("code")) {
    // VSCode with line number
    snprintf(command, sizeof(command), "code -g \"%s:%d\" -r", file_path,
             line_number);
    success = 1;
  } else if (is_editor_available_for_rg("gedit")) {
    // gedit as last resort (limited line number support)
    snprintf(command, sizeof(command), "gedit +%d \"%s\"", line_number, file_path);
    success = 1;
  }

  if (success) {
    // Clear the screen before launching the editor
    system("clear");

    // Execute the command directly in the current terminal
    int result = system(command);
    success = (result == 0);

    return success;
  } else {
    // No suitable editor found
    printf("No compatible editor (neovim, vim, nano, VSCode, gedit) found.\n");
    return 0;
  }
}

static int parse_rg_result(const char *result_line, char *file_path,
                           size_t file_path_size, int *line_number) {
  // Ripgrep output format is: file:line:column:text
  const char *first_colon = strchr(result_line, ':');
  if (!first_colon)
    return 0;

  const char *second_colon = strchr(first_colon + 1, ':');
  if (!second_colon)
    return 0;

  // Extract file path
  size_t path_length = first_colon - result_line;
  if (path_length >= file_path_size)
    path_length = file_path_size - 1;
  strncpy(file_path, result_line, path_length);
  file_path[path_length] = '\0';

  // Extract line number
  char line_str[16] = {0};
  size_t line_str_length = second_colon - (first_colon + 1);
  if (line_str_length >= sizeof(line_str))
    line_str_length = sizeof(line_str) - 1;
  strncpy(line_str, first_colon + 1, line_str_length);
  line_str[line_str_length] = '\0';

  *line_number = atoi(line_str);
  return 1;
}

char *run_interactive_ripgrep(char **args) {
  // Check if ripgrep is installed
  if (!is_rg_installed()) {
    show_rg_install_instructions();
    return NULL;
  }

  // Create temporary files for the search results
  char temp_rg_results[PATH_MAX] = "/tmp/rg_results.txt";
  char temp_fzf_output[PATH_MAX] = "/tmp/rg_selected.txt";

  // Build the initial command
  char command[4096] = {0};

  // Base ripgrep command with sensible defaults
  strcpy(command,
         "rg --line-number --column --no-heading --color=always --smart-case");

  // Add any user-provided arguments
  if (args && args[1] != NULL) {
    int i = 1;
    while (args[i] != NULL) {
      strcat(command, " ");

      // If the argument contains spaces, quote it
      if (strchr(args[i], ' ') != NULL) {
        strcat(command, "\"");
        strcat(command, args[i]);
        strcat(command, "\"");
      } else {
        strcat(command, args[i]);
      }
      i++;
    }
  } else {
    // If no search pattern provided, use empty string to search all files
    // Will be filtered by fzf as user types
    strcat(command, " \"\"");
  }

  // Pipe to fzf for interactive selection with preview
  strcat(command, " | fzf --ansi");

  // Add keybindings for navigation
  strcat(command, " --bind=\"ctrl-j:down,ctrl-k:up,/:toggle-search\"");

  // Add preview window showing file content with line highlighted
  strcat(command, " --preview=\"bat --color=always --style=numbers "
                  "--highlight-line={2} {1}\"");
  strcat(command, " --preview-window=+{2}-10");

  // Add output redirection
  strcat(command, " > ");
  strcat(command, temp_fzf_output);

  // Run the command
  printf("Starting interactive ripgrep search...\n");
  int result = system(command);

  // Check if user canceled (fzf returns non-zero)
  if (result != 0) {
    unlink(temp_fzf_output);
    return NULL;
  }

  // Read the selected result from the temporary file
  FILE *fp = fopen(temp_fzf_output, "r");
  if (!fp) {
    unlink(temp_fzf_output);
    return NULL;
  }

  // Read the selected line
  char *selected = (char *)malloc(PATH_MAX * 2);
  if (!selected) {
    fclose(fp);
    unlink(temp_fzf_output);
    return NULL;
  }

  if (fgets(selected, PATH_MAX * 2, fp) == NULL) {
    fclose(fp);
    unlink(temp_fzf_output);
    free(selected);
    return NULL;
  }

  // Remove newline if present
  size_t len = strlen(selected);
  if (len > 0 && selected[len - 1] == '\n') {
    selected[len - 1] = '\0';
  }

  // Close and delete the temporary file
  fclose(fp);
  unlink(temp_fzf_output);

  return selected;
}

void run_ripgrep_interactive_session(void) {
  // Check if ripgrep and fzf are installed
  if (!is_rg_installed()) {
    show_rg_install_instructions();
    return;
  }

  // Initialize the search query
  char search_query[256] = "";
  char last_query[256] = "";

  // Create a temporary file for results
  char temp_results[PATH_MAX] = "/tmp/ripgrep_results.txt";

  // Save original terminal settings to restore later
  struct termios old_tio, new_tio;
  tcgetattr(STDIN_FILENO, &old_tio);
  
  // Copy the old settings and modify for raw input
  new_tio = old_tio;
  new_tio.c_lflag &= ~(ICANON | ECHO); // Disable canonical mode and echo
  new_tio.c_cc[VMIN] = 1;             // Read one character at a time
  new_tio.c_cc[VTIME] = 0;            // No timeout
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

  // Clear screen and show initial UI
  system("clear");
  printf("--- Interactive Ripgrep Search ---\n");
  printf(
      "Type to search | Ctrl+N/P: navigate | Enter: open | Ctrl+C: exit\n\n");
  printf("Search: ");

  int running = 1;
  int selected_index = 0;
  int results_count = 0;
  char **results = NULL;

  // Main interaction loop
  while (running) {
    // Display the current search query
    printf("\rSearch: %s", search_query);
    fflush(stdout);

    // Check if the query has changed and we need to run ripgrep again
    if (strcmp(search_query, last_query) != 0) {
      // Free previous results if any
      if (results) {
        for (int i = 0; i < results_count; i++) {
          free(results[i]);
        }
        free(results);
        results = NULL;
        results_count = 0;
      }

      // Execute ripgrep with the current query if not empty
      if (strlen(search_query) > 0) {
        // Build ripgrep command
        char command[4096];
        snprintf(command, sizeof(command),
                 "rg --line-number --column --no-heading --color=never "
                 "--smart-case \"%s\" > %s",
                 search_query, temp_results);

        system(command);

        // Read results from the temp file
        FILE *fp = fopen(temp_results, "r");
        if (fp) {
          // Count lines first
          char line[1024];
          results_count = 0;

          while (fgets(line, sizeof(line), fp) != NULL) {
            results_count++;
          }

          // Allocate memory for results
          rewind(fp);
          results = (char **)malloc(results_count * sizeof(char *));

          if (results) {
            int i = 0;
            while (i < results_count && fgets(line, sizeof(line), fp) != NULL) {
              // Remove newline
              size_t len = strlen(line);
              if (len > 0 && line[len - 1] == '\n') {
                line[len - 1] = '\0';
              }

              results[i] = strdup(line);
              i++;
            }
          }

          fclose(fp);
        }
      }

      // Update last query
      strcpy(last_query, search_query);

      // Reset selection to first result
      selected_index = 0;

      // Clear the results display area
      printf("\n\033[J"); // Clear from cursor to end of screen

      // Display results count
      printf("\nFound %d matches\n\n", results_count);

      // Display results with selection
      int max_display = 10; // Show at most 10 results at a time
      for (int i = 0; i < results_count && i < max_display; i++) {
        if (i == selected_index) {
          printf("\033[7m> %s\033[0m\n",
                 results[i]); // Invert colors for selection
        } else {
          printf("  %s\n", results[i]);
        }
      }
    }

    // Process user input
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) > 0) {
      if (c == 3) { // Ctrl+C
        running = 0;
      } else if (c == 13 || c == 10) { // Enter or newline
        // Open the selected file if there are results
        if (results_count > 0 && selected_index < results_count) {
          char file_path[PATH_MAX];
          int line_number;

          if (parse_rg_result(results[selected_index], file_path,
                              sizeof(file_path), &line_number)) {
            rg_open_in_editor(file_path, line_number);

            // After returning from editor, refresh the UI
            system("clear");
            printf("--- Interactive Ripgrep Search ---\n");
            printf("Type to search | Ctrl+N/P: navigate | Enter: open | Ctrl+C: "
                   "exit\n\n");
            printf("Search: %s\n\n", search_query);

            // Re-display results count
            printf("Found %d matches\n\n", results_count);

            // Re-display results with selection
            int max_display = 10;
            for (int i = 0; i < results_count && i < max_display; i++) {
              if (i == selected_index) {
                printf("\033[7m> %s\033[0m\n", results[i]);
              } else {
                printf("  %s\n", results[i]);
              }
            }
          }
        }
      } else if (c == 14) { // Ctrl+N
        if (results_count > 0) {
          selected_index = (selected_index + 1) % results_count;

          // Redraw results with new selection
          printf("\033[%dA",
                 results_count > 10
                     ? 10
                     : results_count); // Move cursor up to results start

          int max_display = 10;
          for (int i = 0; i < results_count && i < max_display; i++) {
            if (i == selected_index) {
              printf("\033[2K\033[7m> %s\033[0m\n",
                     results[i]); // Clear line and invert colors
            } else {
              printf("\033[2K  %s\n", results[i]); // Clear line
            }
          }
        }
      } else if (c == 16) { // Ctrl+P
        if (results_count > 0) {
          selected_index = (selected_index - 1 + results_count) % results_count;

          // Redraw results with new selection
          printf("\033[%dA",
                 results_count > 10
                     ? 10
                     : results_count); // Move cursor up to results start

          int max_display = 10;
          for (int i = 0; i < results_count && i < max_display; i++) {
            if (i == selected_index) {
              printf("\033[2K\033[7m> %s\033[0m\n",
                     results[i]); // Clear line and invert colors
            } else {
              printf("\033[2K  %s\n", results[i]); // Clear line
            }
          }
        }
      } else if (c == 127 || c == 8) { // Backspace (127 on Linux, 8 on some terminals)
        // Remove last character from search query
        size_t len = strlen(search_query);
        if (len > 0) {
          search_query[len - 1] = '\0';
        }
      } else if (isprint(c)) { // Printable character
        // Add character to search query
        size_t len = strlen(search_query);
        if (len < sizeof(search_query) - 1) {
          search_query[len] = c;
          search_query[len + 1] = '\0';
        }
      }
    }
  }

  // Clean up
  if (results) {
    for (int i = 0; i < results_count; i++) {
      free(results[i]);
    }
    free(results);
  }

  unlink(temp_results);

  // Restore original terminal settings
  tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

  // Final screen clear
  system("clear");
}

int lsh_ripgrep(char **args) {
  // Check if ripgrep is installed
  if (!is_rg_installed()) {
    printf("Ripgrep (rg) is not installed. Falling back to custom "
           "implementation.\n");
    printf("For better performance, consider installing ripgrep:\n");
    show_rg_install_instructions();
    printf("\nRunning with custom implementation...\n\n");

    // Fall back to our custom implementation
    run_ripgrep_interactive_session();
    return 1;
  }

  // Also check if fzf is installed for the interactive UI
  int fzf_available = is_fzf_installed(); // Reuse from fzf_native.h

  // Check for help flag
  if (args[1] &&
      (strcmp(args[1], "--help") == 0 || strcmp(args[1], "-h") == 0)) {
    printf("Usage: ripgrep [pattern] [options]\n");
    printf("Interactive code search using ripgrep (rg) with fzf.\n\n");
    printf("If called without arguments, launches fzf with ripgrep for "
           "interactive searching.\n\n");
    printf("Options:\n");
    printf("  -t, --type [TYPE]    Only search files matching TYPE (e.g., -t "
           "cpp)\n");
    printf("  -i, --ignore-case    Case insensitive search\n");
    printf("  -w, --word-regexp    Only match whole words\n");
    printf("  -e, --regexp         Treat pattern as a regular expression\n");
    printf("  -f, --fixed-strings  Treat pattern as a literal string\n");
    printf("  -g, --glob [GLOB]    Include/exclude files matching the glob\n");
    return 1;
  }

  // If no arguments provided, use fzf with ripgrep
  if (!args[1]) {
    if (!fzf_available) {
      printf("fzf is not installed. Falling back to custom implementation.\n");
      show_fzf_install_instructions();
      printf("\nRunning with custom implementation...\n\n");

      // Fall back to custom implementation if fzf is not available
      run_ripgrep_interactive_session();
      return 1;
    }

    // Create a temporary file for the fzf preview script
    char preview_script[PATH_MAX] = "/tmp/fzf_preview.sh";

    // Create the preview script as a shell script with highlighting support
    FILE *preview_fp = fopen(preview_script, "w");
    if (preview_fp) {
      // Write a shell script that highlights searched content
      fprintf(preview_fp, "#!/bin/bash\n");
      fprintf(preview_fp, "file=\"$1\"\n");
      fprintf(preview_fp, "line=\"$2\"\n");
      fprintf(preview_fp, "query=\"$3\"\n");
      fprintf(preview_fp, "if [ -z \"$query\" ]; then\n");
      // If no query, just show the file with line highlighting
      fprintf(preview_fp, "  bat --color=always --highlight-line \"$line\" \"$file\" 2>/dev/null || cat \"$file\"\n");
      fprintf(preview_fp, "else\n");
      // If query provided, highlight both the line and the search term
      fprintf(preview_fp, "  if grep -i \"$query\" \"$file\" >/dev/null 2>&1; then\n");
      fprintf(preview_fp, "    rg --color=always --context 3 --line-number \"$query\" \"$file\" 2>/dev/null || ");
      fprintf(preview_fp, "bat --color=always --highlight-line \"$line\" \"$file\" 2>/dev/null || cat \"$file\"\n");
      fprintf(preview_fp, "  else\n");
      fprintf(preview_fp, "    bat --color=always --highlight-line \"$line\" \"$file\" 2>/dev/null || cat \"$file\"\n");
      fprintf(preview_fp, "  fi\n");
      fprintf(preview_fp, "fi\n");
      fclose(preview_fp);
      
      // Make the script executable
      chmod(preview_script, 0755);
    }

    // Use fzf with ripgrep to provide an interactive UI similar to Neovim
    char command[4096] = {0};

    // Use ripgrep with an empty pattern to search all text files
    // and pipe to fzf for interactive filtering - now with fullscreen mode
    snprintf(
        command, sizeof(command),
        "clear && rg --line-number --column --no-heading --color=always \"\" | "
        "fzf --ansi --delimiter : "
        "--preview \"%s {1} {2} {q}\" "
        "--preview-window=right:60%%:wrap "
        "--bind \"ctrl-j:down,ctrl-k:up,enter:accept\" "
        "--border "
        "--height=100%% > %s",
        preview_script, "/tmp/rg_selection.txt");

    // Execute the command
    int result = system(command);

    // Check if user selected something (fzf returns non-zero on cancel)
    if (result == 0) {
      // Read the selected file/line from the temporary file
      FILE *fp = fopen("/tmp/rg_selection.txt", "r");
      if (fp) {
        char selected[1024];
        if (fgets(selected, sizeof(selected), fp)) {
          // Remove newline if present
          size_t len = strlen(selected);
          if (len > 0 && selected[len - 1] == '\n') {
            selected[len - 1] = '\0';
          }

          // Parse the selection to extract file path and line number
          char file_path[PATH_MAX];
          int line_number;

          if (parse_rg_result(selected, file_path, sizeof(file_path),
                              &line_number)) {
            printf("Opening %s at line %d\n", file_path, line_number);
            rg_open_in_editor(file_path, line_number);
          }
        }
        fclose(fp);
        unlink("/tmp/rg_selection.txt"); // Clean up
      }
    }

    // Remove the preview script
    unlink(preview_script);

    return 1;
  }

  // If options are provided but no pattern, run regular ripgrep
  if (args[1][0] == '-') {
    // Build command to pass to system
    char command[4096] = "rg";

    // Add all arguments
    for (int i = 1; args[i] != NULL; i++) {
      strcat(command, " ");

      // If argument contains spaces, quote it
      if (strchr(args[i], ' ') != NULL) {
        strcat(command, "\"");
        strcat(command, args[i]);
        strcat(command, "\"");
      } else {
        strcat(command, args[i]);
      }
    }

    // Execute standard ripgrep command
    system(command);
    return 1;
  }

  // If a pattern is provided, run ripgrep with fzf for selection
  if (fzf_available) {
    // Create a temporary file for the fzf preview script
    char preview_script[PATH_MAX] = "/tmp/fzf_preview.sh";

    // Create the preview script as a shell script with highlighting
    FILE *preview_fp = fopen(preview_script, "w");
    if (preview_fp) {
      // Write a shell script that highlights searched content
      fprintf(preview_fp, "#!/bin/bash\n");
      fprintf(preview_fp, "file=\"$1\"\n");
      fprintf(preview_fp, "line=\"$2\"\n");
      fprintf(preview_fp, "search_term=\"%s\"\n", args[1]); // Use the search term provided
      // Show file with both line highlighting and search term highlighting
      fprintf(preview_fp, 
              "rg --color=always --context 3 --line-number \"%s\" \"$file\" 2>/dev/null || ", 
              args[1]);
      fprintf(preview_fp, "bat --color=always --highlight-line \"$line\" \"$file\" 2>/dev/null || cat \"$file\"\n");
      fclose(preview_fp);
      
      // Make the script executable
      chmod(preview_script, 0755);
    }

    // Build command with fzf for better interaction
    char command[4096] = {0};

    // Now with better UI but using compatible options
    snprintf(
        command, sizeof(command),
        "clear && rg --line-number --column --no-heading --color=always \"%s\" | "
        "fzf --ansi --delimiter : "
        "--preview \"%s {1} {2}\" "
        "--preview-window=right:60%%:wrap "
        "--bind \"ctrl-j:down,ctrl-k:up,enter:accept\" "
        "--border "
        "--height=100%% > %s",
        args[1], preview_script, "/tmp/rg_selection.txt");

    // Execute the command
    int result = system(command);

    // Check if user selected something
    if (result == 0) {
      // Read the selected file/line from the temporary file
      FILE *fp = fopen("/tmp/rg_selection.txt", "r");
      if (fp) {
        char selected[1024];
        if (fgets(selected, sizeof(selected), fp)) {
          // Remove newline if present
          size_t len = strlen(selected);
          if (len > 0 && selected[len - 1] == '\n') {
            selected[len - 1] = '\0';
          }

          // Parse the selection to extract file path and line number
          char file_path[PATH_MAX];
          int line_number;

          if (parse_rg_result(selected, file_path, sizeof(file_path),
                              &line_number)) {
            printf("Opening %s at line %d\n", file_path, line_number);
            rg_open_in_editor(file_path, line_number);
          }
        }
        fclose(fp);
        unlink("/tmp/rg_selection.txt"); // Clean up
      }
    }

    // Remove the preview script
    unlink(preview_script);
  } else {
    // If fzf is not available, fall back to custom implementation
    printf("fzf is not installed. Falling back to custom implementation.\n");
    run_ripgrep_interactive_session();
  }

  return 1;
}