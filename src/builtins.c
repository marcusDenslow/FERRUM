
#include "builtins.h"
#include "common.h"
#include "diff_viewer.h"
#include "ncurses_diff_viewer.h"
#include "filters.h"
#include "fzf_native.h"
#include "git_integration.h"
#include "grep.h"
#include "persistent_history.h"
#include "structured_data.h"
#include "themes.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// History variables
HistoryEntry command_history[HISTORY_SIZE];
int history_count = 0;
int history_index = 0;

// String array of built-in command names
char *builtin_str[] = {
    "cd",       "help",      "exit",      "dir",        "clear",
    "mkdir",    "rmdir",     "del",       "touch",      "pwd",
    "cat",      "history",   "copy",      "move",       "paste",
    "ps",       "news",      "alias",     "unalias",    "aliases",
    "bookmark", "bookmarks", "goto",      "unbookmark", "focus_timer",
    "weather",  "grep",      "grep-text", "ripgrep",    "fzf",
    "clip",     "echo",      "theme",     "loc",        "git_status",
    "gg",       "ls",        "stats",     "monitor",
};

// Array of function pointers to built-in command implementations
int (*builtin_func[])(char **) = {
    &lsh_cd,          &lsh_help,       &lsh_exit,       &lsh_dir,
    &lsh_clear,       &lsh_mkdir,      &lsh_rmdir,      &lsh_del,
    &lsh_touch,       &lsh_pwd,        &lsh_cat,        &lsh_history,
    &lsh_copy,        &lsh_move,       &lsh_paste,      &lsh_ps,
    &lsh_news,        &lsh_alias,      &lsh_unalias,    &lsh_aliases,
    &lsh_bookmark,    &lsh_bookmarks,  &lsh_goto,       &lsh_unbookmark,
    &lsh_focus_timer, &lsh_weather,    &lsh_grep,       &lsh_actual_grep,
    &lsh_ripgrep,     &lsh_fzf_native, &lsh_clip,       &lsh_echo,
    &lsh_theme,       &lsh_loc,        &lsh_git_status, &lsh_gg,
    lsh_dir,
    &lsh_stats,       &builtin_monitor,
};

void set_color(int color) {
  switch (color) {
  case 0:
    printf(ANSI_COLOR_RESET);
    break;
  case 1:
    printf(ANSI_COLOR_RED);
    break;
  case 2:
    printf(ANSI_COLOR_GREEN);
    break;
  case 3:
    printf(ANSI_COLOR_YELLOW);
    break;
  case 4:
    printf(ANSI_COLOR_BLUE);
    break;
  case 5:
    printf(ANSI_COLOR_MAGENTA);
    break;
  case 6:
    printf(ANSI_COLOR_CYAN);
    break;
  case 7:
    printf(ANSI_COLOR_WHITE);
    break;
  default:
    printf(ANSI_COLOR_RESET);
  }
}

void reset_color() { printf(ANSI_COLOR_RESET); }

int lsh_num_builtins() { return sizeof(builtin_str) / sizeof(char *); }

void lsh_add_to_history(const char *command) {
  // Don't add empty commands or duplicates of the last command
  if (!command || *command == '\0' ||
      (history_count > 0 &&
       strcmp(command_history[history_index - 1].command, command) == 0)) {
    return;
  }

  // Free the oldest entry if we're overwriting it
  if (history_count == HISTORY_SIZE) {
    free(command_history[history_index].command);
  } else {
    history_count++;
  }

  // Add the new command
  command_history[history_index].command = strdup(command);
  command_history[history_index].timestamp = time(NULL);

  // Update the index
  history_index = (history_index + 1) % HISTORY_SIZE;
}

int lsh_cd(char **args) {
  if (args[1] == NULL) {
    // No argument provided, change to home directory
    char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
      fprintf(stderr, "lsh: HOME environment variable not set\n");
      return 1;
    }
    if (chdir(home_dir) != 0) {
      perror("lsh: cd");
    }
  } else {
    if (chdir(args[1]) != 0) {
      perror("lsh: cd");
    }
  }
  return 1;
}

int lsh_help(char **args) {
  if (args[1] != NULL) {
    // Command-specific help
    if (strcmp(args[1], "cd") == 0) {
      printf("cd - Change directory\n");
      printf("Usage: cd [directory]\n");
      printf("  cd          - change to home directory\n");
      printf("  cd <dir>    - change to specified directory\n");
    } else if (strcmp(args[1], "cat") == 0) {
      printf("cat - Display file contents\n");
      printf("Usage: cat <file>\n");
      printf("  Displays the contents of the specified file\n");
    } else if (strcmp(args[1], "grep") == 0) {
      printf("grep - Search for text patterns in files\n");
      printf("Usage: grep <pattern> <file>\n");
      printf("  Searches for the specified pattern in the given file\n");
    } else if (strcmp(args[1], "gg") == 0) {
      printf("gg - Git command shortcuts\n");
      printf("Usage: gg <command>\n");
      printf("Available commands:\n");
      printf("  s   - status (enhanced git status)\n");
      printf("  c   - commit\n");
      printf("  p   - pull\n");
      printf("  ps  - push\n");
      printf("  a   - add .\n");
      printf("  l   - log\n");
      printf("  d   - diff\n");
      printf("  dd  - ncurses diff viewer\n");
      printf("  b   - branch\n");
      printf("  ch  - checkout\n");
      printf("  o   - open repository in browser\n");
    } else if (strcmp(args[1], "weather") == 0) {
      printf("weather - Shows weather information\n");
      printf("Usage:\n");
      printf("  weather        - shows weather for your current location\n");
      printf("  weather <city> - shows weather for a specific city\n");
      printf("Examples:\n");
      printf("  weather\n");
      printf("  weather London\n");
      printf("  weather New York\n");
    } else if (strcmp(args[1], "dir") == 0 || strcmp(args[1], "ls") == 0) {
      printf("dir/ls - List directory contents\n");
      printf("Usage: dir\n");
      printf("  Lists files and directories in the current directory\n");
      printf("  Shows file sizes, types, and modification dates in a table format\n");
    } else if (strcmp(args[1], "mkdir") == 0) {
      printf("mkdir - Create directory\n");
      printf("Usage: mkdir <directory>\n");
      printf("  Creates a new directory with the specified name\n");
    } else if (strcmp(args[1], "rmdir") == 0) {
      printf("rmdir - Remove directory\n");
      printf("Usage: rmdir <directory>\n");
      printf("  Removes an empty directory\n");
    } else if (strcmp(args[1], "del") == 0) {
      printf("del - Delete file\n");
      printf("Usage: del <file>\n");
      printf("  Deletes the specified file\n");
    } else if (strcmp(args[1], "touch") == 0) {
      printf("touch - Create file or update timestamp\n");
      printf("Usage: touch <file>\n");
      printf("  Creates a new empty file or updates the timestamp of an existing file\n");
    } else if (strcmp(args[1], "pwd") == 0) {
      printf("pwd - Print working directory\n");
      printf("Usage: pwd\n");
      printf("  Displays the current working directory path\n");
    } else if (strcmp(args[1], "history") == 0) {
      printf("history - Show command history\n");
      printf("Usage: history\n");
      printf("  Displays the list of previously executed commands with timestamps\n");
    } else if (strcmp(args[1], "copy") == 0) {
      printf("copy - Copy file\n");
      printf("Usage: copy <source> <destination>\n");
      printf("  Copies a file from source to destination\n");
    } else if (strcmp(args[1], "move") == 0) {
      printf("move - Move/rename file\n");
      printf("Usage: move <source> <destination>\n");
      printf("  Moves or renames a file from source to destination\n");
    } else if (strcmp(args[1], "clear") == 0) {
      printf("clear - Clear screen\n");
      printf("Usage: clear\n");
      printf("  Clears the terminal screen\n");
    } else if (strcmp(args[1], "echo") == 0) {
      printf("echo - Display text\n");
      printf("Usage: echo [text...]\n");
      printf("  Displays the specified text to the terminal\n");
    } else if (strcmp(args[1], "alias") == 0) {
      printf("alias - Create command alias\n");
      printf("Usage: alias <name> <command>\n");
      printf("  Creates a shortcut alias for a command\n");
    } else if (strcmp(args[1], "bookmark") == 0) {
      printf("bookmark - Bookmark current directory\n");
      printf("Usage: bookmark <name>\n");
      printf("  Saves the current directory with a bookmark name\n");
    } else if (strcmp(args[1], "goto") == 0) {
      printf("goto - Go to bookmarked directory\n");
      printf("Usage: goto <name>\n");
      printf("  Changes to a previously bookmarked directory\n");
    } else if (strcmp(args[1], "theme") == 0) {
      printf("theme - Change shell theme\n");
      printf("Usage: theme <theme_name>\n");
      printf("  Changes the visual appearance of the shell\n");
    } else if (strcmp(args[1], "loc") == 0) {
      printf("loc - Count lines of code\n");
      printf("Usage: loc <file>\n");
      printf("  Counts total lines, code lines, comments, and blank lines in a file\n");
    } else if (strcmp(args[1], "monitor") == 0) {
      printf("monitor - System monitor\n");
      printf("Usage: monitor\n");
      printf("  Displays real-time system information including CPU, memory, and disk usage\n");
    } else if (strcmp(args[1], "stats") == 0) {
      printf("stats - Command usage statistics\n");
      printf("Usage: stats\n");
      printf("  Shows statistics about your most frequently used commands\n");
    } else if (strcmp(args[1], "help") == 0) {
      printf("help - Display help information\n");
      printf("Usage:\n");
      printf("  help           - show all available commands\n");
      printf("  help <command> - show help for a specific command\n");
    } else if (strcmp(args[1], "exit") == 0) {
      printf("exit - Exit the shell\n");
      printf("Usage: exit\n");
      printf("  Terminates the shell session\n");
    } else if (strcmp(args[1], "ps") == 0) {
      printf("ps - List running processes\n");
      printf("Usage: ps\n");
      printf("  Displays a list of all running processes on the system\n");
    } else if (strcmp(args[1], "news") == 0) {
      printf("news - Show latest repository updates\n");
      printf("Usage: news\n");
      printf("  Fetches and displays the latest commit information from the GitHub repository\n");
    } else if (strcmp(args[1], "unalias") == 0) {
      printf("unalias - Remove command alias\n");
      printf("Usage: unalias <name>\n");
      printf("  Removes a previously created command alias\n");
    } else if (strcmp(args[1], "aliases") == 0) {
      printf("aliases - List all aliases\n");
      printf("Usage: aliases\n");
      printf("  Displays all currently defined command aliases\n");
    } else if (strcmp(args[1], "bookmarks") == 0) {
      printf("bookmarks - List all bookmarks\n");
      printf("Usage: bookmarks\n");
      printf("  Displays all saved directory bookmarks\n");
    } else if (strcmp(args[1], "unbookmark") == 0) {
      printf("unbookmark - Remove bookmark\n");
      printf("Usage: unbookmark <name>\n");
      printf("  Removes a previously saved directory bookmark\n");
    } else if (strcmp(args[1], "focus_timer") == 0) {
      printf("focus_timer - Productivity timer\n");
      printf("Usage: focus_timer [minutes]\n");
      printf("  Starts a focus/pomodoro timer for productivity sessions\n");
    } else if (strcmp(args[1], "grep-text") == 0) {
      printf("grep-text - Alternative text search\n");
      printf("Usage: grep-text <pattern> <file>\n");
      printf("  Alternative implementation for searching text patterns in files\n");
    } else if (strcmp(args[1], "ripgrep") == 0) {
      printf("ripgrep - Fast text search\n");
      printf("Usage: ripgrep <pattern> [path]\n");
      printf("  Fast recursive text search using ripgrep-like functionality\n");
    } else if (strcmp(args[1], "fzf") == 0) {
      printf("fzf - Fuzzy file finder\n");
      printf("Usage: fzf\n");
      printf("  Interactive fuzzy file finder for quick file selection\n");
    } else if (strcmp(args[1], "clip") == 0) {
      printf("clip - Clipboard operations\n");
      printf("Usage: clip\n");
      printf("  Clipboard functionality (currently not implemented)\n");
    } else if (strcmp(args[1], "git_status") == 0) {
      printf("git_status - Git repository status\n");
      printf("Usage: git_status\n");
      printf("  Shows the current Git repository status\n");
    } else if (strcmp(args[1], "paste") == 0) {
      printf("paste - Paste clipboard content\n");
      printf("Usage: paste\n");
      printf("  Paste functionality (currently not implemented)\n");
    } else {
      printf("No help available for '%s'\n", args[1]);
      printf("Type 'help' to see all available commands\n");
    }
    return 1;
  }

  // General help - show all commands
  printf("LSH Shell - A lightweight shell with modern features\n");
  printf("Type a command and press Enter to execute it.\n");
  printf("The following built-in commands are available:\n\n");

  // Sort commands alphabetically
  char *sorted_commands[lsh_num_builtins()];
  for (int i = 0; i < lsh_num_builtins(); i++) {
    sorted_commands[i] = builtin_str[i];
  }

  for (int i = 0; i < lsh_num_builtins() - 1; i++) {
    for (int j = i + 1; j < lsh_num_builtins(); j++) {
      if (strcmp(sorted_commands[i], sorted_commands[j]) > 0) {
        char *temp = sorted_commands[i];
        sorted_commands[i] = sorted_commands[j];
        sorted_commands[j] = temp;
      }
    }
  }

  // Print commands in columns
  int columns = 4;
  int rows = (lsh_num_builtins() + columns - 1) / columns;
  for (int i = 0; i < rows; i++) {
    for (int j = 0; j < columns; j++) {
      int index = j * rows + i;
      if (index < lsh_num_builtins()) {
        printf("%-15s", sorted_commands[index]);
      }
    }
    printf("\n");
  }

  printf(
      "\nFor more information on specific commands, type 'help <command>'\n");
  printf("Use tab completion for commands and file paths\n");
  printf("Use arrow keys to navigate command history\n");
  printf("Type a partial command followed by '?' for suggestions\n");

  return 1;
}

int lsh_exit(char **args) { return 0; }

int lsh_dir(char **args) {
  DIR *dir;
  struct dirent *entry;
  struct stat file_stat;
  char cwd[PATH_MAX];
  char file_path[PATH_MAX];
  int detailed = 0;

  // Create table headers
  char *headers[] = {"Name", "Size", "Type", "Modified"};
  TableData *table = create_table(headers, 4);
  if (!table) {
    fprintf(stderr, "lsh: failed to create table\n");
    return 1;
  }

  if (getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("lsh: getcwd");
    free_table(table);
    return 1;
  }

  dir = opendir(cwd);
  if (dir == NULL) {
    perror("lsh: opendir");
    free_table(table);
    return 1;
  }

  // Collect all entries
  while ((entry = readdir(dir)) != NULL) {
    // Skip . and .. entries
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    snprintf(file_path, sizeof(file_path), "%s/%s", cwd, entry->d_name);
    if (stat(file_path, &file_stat) == 0) {
      // Create a new row
      DataValue *row = (DataValue *)malloc(4 * sizeof(DataValue));
      if (!row) {
        fprintf(stderr, "lsh: allocation error\n");
        continue;
      }

      // Name column
      row[0].type = TYPE_STRING;
      row[0].value.str_val = strdup(entry->d_name);
      row[0].is_highlighted =
          S_ISDIR(file_stat.st_mode); // Highlight directories

      // Size column
      row[1].type = TYPE_SIZE;
      row[1].value.str_val = malloc(32);
      if (S_ISDIR(file_stat.st_mode)) {
        strcpy(row[1].value.str_val, "<DIR>");
      } else if (file_stat.st_size < 1024) {
        snprintf(row[1].value.str_val, 32, "%d B", (int)file_stat.st_size);
      } else if (file_stat.st_size < 1024 * 1024) {
        snprintf(row[1].value.str_val, 32, "%.1f KB",
                 file_stat.st_size / 1024.0);
      } else {
        snprintf(row[1].value.str_val, 32, "%.1f MB",
                 file_stat.st_size / (1024.0 * 1024.0));
      }
      row[1].is_highlighted = 0;

      // Type column
      row[2].type = TYPE_STRING;
      if (S_ISDIR(file_stat.st_mode)) {
        row[2].value.str_val = strdup("Directory");
      } else if (S_ISREG(file_stat.st_mode)) {
        // Try to determine file type by extension
        char *ext = strrchr(entry->d_name, '.');
        if (ext != NULL) {
          ext++; // Skip the dot
          if (strcasecmp(ext, "c") == 0 || strcasecmp(ext, "cpp") == 0 ||
              strcasecmp(ext, "h") == 0 || strcasecmp(ext, "hpp") == 0) {
            row[2].value.str_val = strdup("Source");
          } else if (strcasecmp(ext, "exe") == 0 ||
                     strcasecmp(ext, "bat") == 0 ||
                     strcasecmp(ext, "sh") == 0 ||
                     strcasecmp(ext, "com") == 0) {
            row[2].value.str_val = strdup("Executable");
          } else if (strcasecmp(ext, "txt") == 0 ||
                     strcasecmp(ext, "md") == 0 ||
                     strcasecmp(ext, "log") == 0) {
            row[2].value.str_val = strdup("Text");
          } else if (strcasecmp(ext, "jpg") == 0 ||
                     strcasecmp(ext, "png") == 0 ||
                     strcasecmp(ext, "gif") == 0 ||
                     strcasecmp(ext, "bmp") == 0) {
            row[2].value.str_val = strdup("Image");
          } else {
            row[2].value.str_val = strdup("File");
          }
        } else {
          row[2].value.str_val = strdup("File");
        }
      } else {
        row[2].value.str_val = strdup("Special");
      }
      row[2].is_highlighted = 0;

      // Modified date column
      row[3].type = TYPE_STRING;
      row[3].value.str_val = malloc(32);
      struct tm *tm_info = localtime(&file_stat.st_mtime);
      strftime(row[3].value.str_val, 32, "%Y-%m-%d %H:%M", tm_info);
      row[3].is_highlighted = 0;

      // Add row to table
      add_table_row(table, row);
    }
  }

  closedir(dir);

  // Print the table
  print_table(table);

  // Free the table
  free_table(table);

  return 1;
}

int lsh_clear(char **args) {
  printf(ANSI_CLEAR_SCREEN);
  printf(ANSI_CURSOR_HOME);
  return 1;
}

int lsh_mkdir(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"mkdir\"\n");
  } else {
    if (mkdir(args[1], 0755) != 0) {
      perror("lsh: mkdir");
    }
  }
  return 1;
}

int lsh_rmdir(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"rmdir\"\n");
  } else {
    if (rmdir(args[1]) != 0) {
      perror("lsh: rmdir");
    }
  }
  return 1;
}

int lsh_del(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"del\"\n");
  } else {
    if (unlink(args[1]) != 0) {
      perror("lsh: del");
    }
  }
  return 1;
}

int lsh_touch(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"touch\"\n");
  } else {
    int fd = open(args[1], O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd == -1) {
      perror("lsh: touch");
    } else {
      close(fd);
    }
  }
  return 1;
}

int lsh_pwd(char **args) {
  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("%s\n", cwd);
  } else {
    perror("lsh: getcwd");
  }
  return 1;
}

int lsh_cat(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"cat\"\n");
    return 1;
  }

  FILE *fp = fopen(args[1], "r");
  if (fp == NULL) {
    perror("lsh: cat");
    return 1;
  }

  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), fp) != NULL) {
    printf("%s", buffer);
  }

  fclose(fp);
  return 1;
}

int lsh_history(char **args) {
  char time_str[20];
  struct tm *tm_info;

  printf("Command History:\n");
  printf("----------------\n");

  // Display history in chronological order
  for (int i = 0; i < history_count; i++) {
    int idx = (history_index - history_count + i + HISTORY_SIZE) % HISTORY_SIZE;
    tm_info = localtime(&command_history[idx].timestamp);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    printf("%3d: [%s] %s\n", i + 1, time_str, command_history[idx].command);
  }

  return 1;
}

int lsh_copy(char **args) {
  if (args[1] == NULL || args[2] == NULL) {
    fprintf(stderr,
            "lsh: expected source and destination arguments to \"copy\"\n");
    return 1;
  }

  FILE *source = fopen(args[1], "rb");
  if (source == NULL) {
    perror("lsh: copy (source)");
    return 1;
  }

  FILE *dest = fopen(args[2], "wb");
  if (dest == NULL) {
    fclose(source);
    perror("lsh: copy (destination)");
    return 1;
  }

  char buffer[4096];
  size_t bytes_read;
  while ((bytes_read = fread(buffer, 1, sizeof(buffer), source)) > 0) {
    fwrite(buffer, 1, bytes_read, dest);
  }

  fclose(source);
  fclose(dest);
  printf("Copied %s to %s\n", args[1], args[2]);
  return 1;
}

int lsh_move(char **args) {
  if (args[1] == NULL || args[2] == NULL) {
    fprintf(stderr,
            "lsh: expected source and destination arguments to \"move\"\n");
    return 1;
  }

  if (rename(args[1], args[2]) != 0) {
    perror("lsh: move");
    return 1;
  }

  printf("Moved %s to %s\n", args[1], args[2]);
  return 1;
}

int lsh_paste(char **args) {
  printf("Paste functionality not implemented yet\n");
  return 1;
}

int lsh_ps(char **args) {
  FILE *fp = popen("ps -ef", "r");
  if (fp == NULL) {
    perror("lsh: ps");
    return 1;
  }

  char buffer[1024];
  while (fgets(buffer, sizeof(buffer), fp) != NULL) {
    printf("%s", buffer);
  }

  pclose(fp);
  return 1;
}

char *unescape_json_string(const char *str) {
  if (!str)
    return NULL;

  size_t len = strlen(str);
  char *result = malloc(len + 1);
  if (!result)
    return NULL;

  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    if (str[i] == '\\' && i + 1 < len) {
      switch (str[i + 1]) {
      case 'n':
        result[j++] = '\n';
        i++; // Skip the next character
        break;
      case 't':
        result[j++] = '\t';
        i++;
        break;
      case 'r':
        result[j++] = '\r';
        i++;
        break;
      case '\\':
        result[j++] = '\\';
        i++;
        break;
      case '"':
        result[j++] = '"';
        i++;
        break;
      default:
        // Keep the backslash if it's not a recognized escape sequence
        result[j++] = str[i];
      }
    } else {
      result[j++] = str[i];
    }
  }
  result[j] = '\0';

  return result;
}

int lsh_news(char **args) {
  printf("Fetching latest updates from shelltestLinux repository...\n\n");

  // Use GitHub API to get the latest commit from the main branch
  const char *api_url =
      "https://api.github.com/repos/marcusDenslow/shelltestLinux/commits/main";
  char command[1024];
  snprintf(command, sizeof(command),
           "curl -s -H \"Accept: application/vnd.github.v3+json\" \"%s\"",
           api_url);

  FILE *fp = popen(command, "r");
  if (!fp) {
    fprintf(
        stderr,
        "Failed to fetch updates. Please check your internet connection.\n");
    return 1;
  }

  // Read the entire response
  char response[8192] = "";
  size_t total_read = 0;
  size_t chunk_size;

  while ((chunk_size = fread(response + total_read, 1,
                             sizeof(response) - total_read - 1, fp)) > 0) {
    total_read += chunk_size;
    if (total_read >= sizeof(response) - 1)
      break;
  }
  response[total_read] = '\0';
  pclose(fp);

  // Check if we got an error response
  if (strstr(response, "\"message\":") &&
      strstr(response, "\"documentation_url\":")) {
    fprintf(stderr, "Failed to fetch updates from GitHub. The repository might "
                    "be private or there might be a connection issue.\n");
    return 1;
  }

  // Extract commit information from JSON
  char *sha = extract_json_string(response, "sha");
  char *message_raw = extract_json_string(response, "message");
  char *message = NULL;
  if (message_raw) {
    message = unescape_json_string(message_raw);
    free(message_raw);
  }

  char *author_name = NULL;
  char *author_email = NULL;
  char *date = NULL;

  // Find author object
  char *author_start = strstr(response, "\"author\":");
  if (author_start) {
    author_name = extract_json_string(author_start, "name");
    author_email = extract_json_string(author_start, "email");
    date = extract_json_string(author_start, "date");
  }

  // Format the date if we have it
  char formatted_date[64] = "Unknown date";
  if (date) {
    // Parse the ISO date (e.g., "2024-01-20T15:30:45Z")
    struct tm tm_info = {0};
    if (sscanf(date, "%d-%d-%dT%d:%d:%d", &tm_info.tm_year, &tm_info.tm_mon,
               &tm_info.tm_mday, &tm_info.tm_hour, &tm_info.tm_min,
               &tm_info.tm_sec) == 6) {
      tm_info.tm_year -= 1900; // tm_year is years since 1900
      tm_info.tm_mon -= 1;     // tm_mon is 0-based

      time_t commit_time = mktime(&tm_info);
      time_t now = time(NULL);
      double diff_seconds = difftime(now, commit_time);

      // Format as relative time
      if (diff_seconds < 60) {
        snprintf(formatted_date, sizeof(formatted_date), "just now");
      } else if (diff_seconds < 3600) {
        int minutes = (int)(diff_seconds / 60);
        snprintf(formatted_date, sizeof(formatted_date), "%d minute%s ago",
                 minutes, minutes == 1 ? "" : "s");
      } else if (diff_seconds < 86400) {
        int hours = (int)(diff_seconds / 3600);
        snprintf(formatted_date, sizeof(formatted_date), "%d hour%s ago", hours,
                 hours == 1 ? "" : "s");
      } else if (diff_seconds < 604800) {
        int days = (int)(diff_seconds / 86400);
        snprintf(formatted_date, sizeof(formatted_date), "%d day%s ago", days,
                 days == 1 ? "" : "s");
      } else {
        // Format as absolute date
        strftime(formatted_date, sizeof(formatted_date), "%Y-%m-%d %H:%M",
                 &tm_info);
      }
    }
  }

  // Display the information
  printf(ANSI_COLOR_CYAN "Latest Update - shelltestLinux\n" ANSI_COLOR_RESET);
  printf("════════════════════════════════════════════════════════════\n\n");

  if (sha && message) {
    // Shorten SHA to first 7 characters
    char short_sha[8] = "";
    strncpy(short_sha, sha, 7);
    short_sha[7] = '\0';

    printf(ANSI_COLOR_GREEN "Commit:" ANSI_COLOR_RESET " %s\n", short_sha);
    printf(ANSI_COLOR_GREEN "Date:" ANSI_COLOR_RESET "   %s\n", formatted_date);

    if (author_name) {
      printf(ANSI_COLOR_GREEN "Author:" ANSI_COLOR_RESET " %s", author_name);
      if (author_email) {
        printf(" <%s>", author_email);
      }
      printf("\n");
    }

    printf("\n" ANSI_COLOR_YELLOW "Message:" ANSI_COLOR_RESET "\n");

    // Split the message into title and description
    char *first_newline = strchr(message, '\n');
    if (first_newline) {
      // We have a title and description
      *first_newline = '\0'; // Temporarily terminate the title
      printf("  " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET "\n\n", message);

      // Print the description
      char *description = first_newline + 1;
      // Skip any leading newlines in the description
      while (*description == '\n')
        description++;

      if (*description) {
        printf("  %s\n", description);
      }
    } else {
      // Just a title, no description
      printf("  " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET "\n", message);
    }

    printf("\n────────────────────────────────────────────────────────────\n");
    printf("Repository: " ANSI_COLOR_CYAN
           "https://github.com/marcusDenslow/shelltestLinux" ANSI_COLOR_RESET
           "\n");
    printf("View commit: "
           "https://github.com/marcusDenslow/shelltestLinux/commit/%s\n",
           short_sha);

  } else {
    fprintf(stderr, "Failed to parse update information. The GitHub API "
                    "response format might have changed.\n");
  }

  // Free allocated strings
  if (sha)
    free(sha);
  if (message)
    free(message);
  if (author_name)
    free(author_name);
  if (author_email)
    free(author_email);
  if (date)
    free(date);

  return 1;
}

int lsh_clip(char **args) {
  printf("Clipboard functionality not implemented yet\n");
  return 1;
}

int lsh_echo(char **args) {
  if (args[1] == NULL) {
    printf("\n");
    return 1;
  }

  for (int i = 1; args[i] != NULL; i++) {
    printf("%s", args[i]);
    if (args[i + 1] != NULL) {
      printf(" ");
    }
  }
  printf("\n");
  return 1;
}

int lsh_loc(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected file or directory argument to \"loc\"\n");
    return 1;
  }

  struct stat st;
  if (stat(args[1], &st) != 0) {
    perror("lsh: loc");
    return 1;
  }

  if (S_ISREG(st.st_mode)) {
    // Count lines in a single file
    FILE *file = fopen(args[1], "r");
    if (!file) {
      perror("lsh: loc");
      return 1;
    }

    int lines = 0, code_lines = 0, blank_lines = 0, comment_lines = 0;
    char line[1024];
    int in_comment_block = 0;

    while (fgets(line, sizeof(line), file)) {
      lines++;
      char *trimmed = line;
      while (*trimmed && isspace(*trimmed))
        trimmed++;

      if (*trimmed == '\0') {
        blank_lines++;
      } else if (strncmp(trimmed, "//", 2) == 0) {
        comment_lines++;
      } else if (strncmp(trimmed, "/*", 2) == 0) {
        comment_lines++;
        in_comment_block = 1;
        if (strstr(trimmed, "*/")) {
          in_comment_block = 0;
        }
      } else if (in_comment_block) {
        comment_lines++;
        if (strstr(trimmed, "*/")) {
          in_comment_block = 0;
        }
      } else {
        code_lines++;
      }
    }

    fclose(file);

    printf("File: %s\n", args[1]);
    printf("Total lines: %d\n", lines);
    printf("Code lines: %d\n", code_lines);
    printf("Comment lines: %d\n", comment_lines);
    printf("Blank lines: %d\n", blank_lines);

  } else if (S_ISDIR(st.st_mode)) {
    printf("Directory LOC counting not implemented yet\n");
  } else {
    fprintf(stderr, "lsh: %s is not a file or directory\n", args[1]);
  }

  return 1;
}

char *extract_json_string(const char *json, const char *key) {
  char search_key[100];
  sprintf(search_key, "\"%s\":", key);

  char *key_pos = strstr(json, search_key);
  if (!key_pos)
    return NULL;

  key_pos += strlen(search_key);

  // Skip whitespace
  while (*key_pos && isspace(*key_pos))
    key_pos++;

  if (*key_pos != '"')
    return NULL;

  key_pos++; // Skip opening quote

  // Find closing quote
  char *end = key_pos;
  while (*end && *end != '"') {
    if (*end == '\\' && *(end + 1)) {
      end += 2; // Skip escaped character
    } else {
      end++;
    }
  }

  if (!*end)
    return NULL;

  // Allocate and copy the string value
  int len = end - key_pos;
  char *result = malloc(len + 1);
  if (!result)
    return NULL;

  strncpy(result, key_pos, len);
  result[len] = '\0';

  return result;
}

int lsh_git_status(char **args) {
  // Show current Git status
  char *git_status = get_git_status();
  if (git_status) {
    printf("Git Status: %s\n", git_status);
    free(git_status);
  } else {
    printf("Not in a Git repository or Git not available\n");
  }
  return 1;
}

int lsh_gg(char **args) {
  if (args[1] == NULL) {
    printf("Usage: gg <command>\n");
    printf("Available commands:\n");
    printf("  s - status\n");
    printf("  c - commit\n");
    printf("  p - pull\n");
    printf("  ps - push\n");
    printf("  a - add .\n");
    printf("  l - log\n");
    printf("  d - diff\n");
    printf("  dd - ncurses diff viewer\n");
    printf("  b - branch\n");
    printf("  ch - checkout\n");
    printf("  o - open in GitHub browser\n");
    return 1;
  }

  // Execute the appropriate git command based on shorthand
  if (strcmp(args[1], "s") == 0) {
    // Enhanced git status with more information
    char branch_name[100] = "";
    char repo_name[100] = "";
    char last_commit_title[256] = "";
    char last_commit_hash[32] = "";
    char recent_commits[2][256];
    char repo_url[512] = "";
    int is_dirty = 0;
    
    // Check if we're in a git repo
    if (!get_git_branch(branch_name, sizeof(branch_name), &is_dirty)) {
        printf("Not in a Git repository\n");
        return 1;
    }
    
    // Get all the information
    get_git_repo_name(repo_name, sizeof(repo_name));
    get_last_commit(last_commit_title, sizeof(last_commit_title), 
                        last_commit_hash, sizeof(last_commit_hash));
    int commit_count = get_recent_commit(recent_commits, 2);
    get_repo_url(repo_url, sizeof(repo_url));
    
    // Display enhanced status
    printf("\n" ANSI_COLOR_CYAN "Git Repository Status" ANSI_COLOR_RESET "\n");
    printf("══════════════════════════════════════════════════\n\n");
    
    // Repository info
    printf(ANSI_COLOR_GREEN "Repo:" ANSI_COLOR_RESET "   %s\n", 
           strlen(repo_name) > 0 ? repo_name : "Unknown");
    
    // Branch info with clickable link
    if (strlen(repo_url) > 0) {
        printf(ANSI_COLOR_GREEN "Branch:" ANSI_COLOR_RESET " \033]8;;%s/tree/%s\033\\%s\033]8;;\033\\%s\n", 
               repo_url, branch_name, branch_name, is_dirty ? " *" : "");
    } else {
        printf(ANSI_COLOR_GREEN "Branch:" ANSI_COLOR_RESET " %s%s\n", 
               branch_name, is_dirty ? " *" : "");
    }
    
    // Last commit info
    if (strlen(last_commit_hash) > 0 && strlen(last_commit_title) > 0) {
        if (strlen(repo_url) > 0) {
            printf(ANSI_COLOR_GREEN "Last commit:" ANSI_COLOR_RESET " \033]8;;%s/commit/%s\033\\%s\033]8;;\033\\ - %s\n", 
                   repo_url, last_commit_hash, last_commit_hash, last_commit_title);
        } else {
            printf(ANSI_COLOR_GREEN "Last commit:" ANSI_COLOR_RESET " %s - %s\n", 
                   last_commit_hash, last_commit_title);
        }
    }
    
    // Recent commits
    if (commit_count > 0) {
        printf(ANSI_COLOR_GREEN "Last %d commits:" ANSI_COLOR_RESET "\n", commit_count);
        for (int i = 0; i < commit_count; i++) {
            printf("  %d. %s\n", i + 1, recent_commits[i]);
        }
    }
    
    // Repository link
    if (strlen(repo_url) > 0) {
        printf(ANSI_COLOR_GREEN "Repository:" ANSI_COLOR_RESET " \033]8;;%s\033\\%s\033]8;;\033\\\n", 
               repo_url, repo_url);
    }
    
    printf("\n");
    
    // Show regular git status output
    printf(ANSI_COLOR_YELLOW "Working Directory Status:" ANSI_COLOR_RESET "\n");
    system("git status --short");
    
    if (!is_dirty) {
        printf(ANSI_COLOR_GREEN "Working tree clean" ANSI_COLOR_RESET "\n");
    }
    
    printf("\n");
  } else if (strcmp(args[1], "b") == 0) {
    system("git branch");
  } else if (strcmp(args[1], "o") == 0) {
    // Get the remote URL
    FILE *fp;
    char remote_url[1024] = {0};
    fp = popen("git config --get remote.origin.url 2>/dev/null", "r");
    if (fp) {
      fgets(remote_url, sizeof(remote_url), fp);
      pclose(fp);

      // Trim trailing newline
      char *newline = strchr(remote_url, '\n');
      if (newline)
        *newline = '\0';

      if (strlen(remote_url) > 0) {
        // Convert SSH URLs to HTTPS
        char https_url[1024] = {0};
        if (strstr(remote_url, "git@github.com:")) {
          // Convert ssh format (git@github.com:user/repo.git) to https
          char *repo_path = strchr(remote_url, ':');
          if (repo_path) {
            repo_path++; // Skip the colon
            // Remove .git suffix if present
            char *git_suffix = strstr(repo_path, ".git");
            if (git_suffix)
              *git_suffix = '\0';

            sprintf(https_url, "https://github.com/%s", repo_path);
          }
        } else if (strstr(remote_url, "https://github.com/")) {
          // Already HTTPS format
          strcpy(https_url, remote_url);
          // Remove .git suffix if present
          char *git_suffix = strstr(https_url, ".git");
          if (git_suffix)
            *git_suffix = '\0';
        }

        if (strlen(https_url) > 0) {
          // Open the URL in the default browser
          char command[1100];
          // Use xdg-open on Linux
          sprintf(command, "xdg-open %s >/dev/null 2>&1", https_url);
          int result = system(command);
          if (result == 0) {
            printf("Opening %s in browser\n", https_url);
          } else {
            printf("Failed to open browser. URL: %s\n", https_url);
          }
        } else {
          printf("Could not parse GitHub URL from: %s\n", remote_url);
        }
      } else {
        printf("No remote URL found. Is this a Git repository with a GitHub "
               "remote?\n");
      }
    } else {
      printf("Not in a Git repository or no remote configured\n");
    }
  } else if (strcmp(args[1], "c") == 0) {
    if (args[2] != NULL) {
      char command[1024];
      sprintf(command, "git commit -m \"%s\"", args[2]);
      system(command);
    } else {
      system("git commit");
    }
  } else if (strcmp(args[1], "p") == 0) {
    system("git pull");
  } else if (strcmp(args[1], "ps") == 0) {
    // Try push without credentials first - force git to fail without prompting
    int result = system("GIT_ASKPASS=/bin/false GIT_TERMINAL_PROMPT=0 git push </dev/null 2>/dev/null");
    
    // If push failed, try with credentials
    if (result != 0) {
      char username[256] = "";
      char token[256] = "";
      
      printf("Push failed. Authentication required.\n");
      printf("GitHub Username: ");
      fflush(stdout);
      
      if (fgets(username, sizeof(username), stdin)) {
        // Remove newline
        username[strcspn(username, "\n")] = 0;
        
        printf("Personal Access Token: ");
        fflush(stdout);
        
        // Hide token input
        system("stty -echo");
        if (fgets(token, sizeof(token), stdin)) {
          token[strcspn(token, "\n")] = 0;
          system("stty echo");
          printf("\n");
          
          printf("Attempting authenticated push...\n");
          result = execute_git_with_auth("git push", username, token);
          
          // Clear credentials from memory for security
          memset(username, 0, sizeof(username));
          memset(token, 0, sizeof(token));
          
          if (result == 0) {
            printf("Push successful!\n");
          } else {
            printf("Push failed. Please check your credentials and try again.\n");
          }
        } else {
          system("stty echo");
          printf("\nAuthentication cancelled.\n");
        }
      } else {
        printf("Authentication cancelled.\n");
      }
    } else {
      printf("Push successful!\n");
    }
  } else if (strcmp(args[1], "a") == 0) {
    system("git add .");
  } else if (strcmp(args[1], "l") == 0) {
    system("git log --oneline -10");
  } else if (strcmp(args[1], "d") == 0) {
    run_diff_viewer();
  } else if (strcmp(args[1], "dd") == 0) {
    run_ncurses_diff_viewer();
  } else if (strcmp(args[1], "b") == 0) {
    system("git branch");
  } else if (strcmp(args[1], "ch") == 0) {
    if (args[2] != NULL) {
      char command[1024];
      sprintf(command, "git checkout %s", args[2]);
      system(command);
    } else {
      printf("Please specify a branch to checkout\n");
    }
  } else if (strcmp(args[1], "debug") == 0) {
    printf("=== Git Debug Log ===\n");
    if (access("/tmp/git_debug.log", F_OK) == 0) {
      system("cat /tmp/git_debug.log");
    } else {
      printf("No debug log found. Try pushing to generate debug info.\n");
    }
    printf("\n=== End Debug Log ===\n");
  } else if (strcmp(args[1], "debug-clear") == 0) {
    system("rm -f /tmp/git_debug.log");
    printf("Debug log cleared.\n");
  } else {
    printf("Unknown git command shorthand: %s\n", args[1]);
  }

  return 1;
}

int lsh_stats(char **args) {
  printf("Command Statistics\n");
  printf("=================\n\n");

  if (frequency_count == 0) {
    printf("No command history available\n");
    return 1;
  }

  typedef struct {
    char *command;
    int count;
    double score;
  } CommandStat;

  CommandStat *stats = malloc(frequency_count * sizeof(CommandStat));
  if (!stats)
    return 1;

  time_t current_time = time(NULL);
  for (int i = 0; i < frequency_count; i++) {
    stats[i].command = command_frequencies[i].command;
    stats[i].count = command_frequencies[i].count;

    time_t most_recent = 0;
    for (int j = history_size - 1; j >= 0; j--) {
      if (strcmp(history_entries[j].command, stats[i].command) == 0) {
        most_recent = history_entries[j].timestamp;
        break;
      }
    }

    double hours_ago = (current_time - most_recent) / 3600.0;
    double recency_weight = hours_ago > 0 ? 1.0 / (1.0 + hours_ago * 0.1) : 1.0;

    stats[i].score = stats[i].count * recency_weight;
    
  }

  for (int i = 0; i < frequency_count - 1; i++) {
    for (int j = i + 1; j < frequency_count; j++) {
      if (stats[j].score > stats[i].score) {
        CommandStat temp = stats[i];
        stats[i] = stats[j];
        stats[j] = temp;
      }
    }
  }

  printf("Top Commands (by frequency + recency):\n");
  printf("Rank  Command                Count    Score\n");
  printf("----  --------------------   -----    -----\n");

  int display_count = frequency_count < 10 ? frequency_count : 10;
  for (int i = 0; i < display_count; i++) {
    printf("%-4d  %-20s    %-5d    %.2f\n", i + 1, stats[i].command,
           stats[i].count, stats[i].score);
  }

  printf("\nNext Command Prediction: \n");
  if (frequency_count > 0) {
    printf("Most likely: %s (score: %.2f)\n", stats[0].command, stats[0].score);
  }
	free(stats);
	return 1;
}
