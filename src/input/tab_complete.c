
#include "tab_complete.h"
#include "aliases.h"
#include "bookmarks.h"
#include "builtins.h"
#include "favorite_cities.h"
#include "themes.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

// Predefined command argument types for better completion
static CommandArgInfo command_arg_info[] = {
    // Built-in shell commands with strict matching for specific argument types
    {"cd", ARG_TYPE_DIRECTORY, "Change current directory", 0},
    {"help", ARG_TYPE_COMMAND, "Display help", 0},
    {"exit", ARG_TYPE_ANY, "Exit the shell", 0},
    {"dir", ARG_TYPE_DIRECTORY, "List directory contents", 0},
    {"clear", ARG_TYPE_ANY, "Clear the screen", 0},
    {"mkdir", ARG_TYPE_DIRECTORY, "Make directory", 0},
    {"rmdir", ARG_TYPE_DIRECTORY, "Remove directory", 0},
    {"del", ARG_TYPE_FILE, "Delete a file", 0},
    {"touch", ARG_TYPE_FILE, "Create or update a file", 0},
    {"pwd", ARG_TYPE_ANY, "Print working directory", 0},
    {"cat", ARG_TYPE_FILE, "Display file contents", 0},
    {"history", ARG_TYPE_ANY, "Display command history", 0},
    {"copy", ARG_TYPE_FILE, "Copy file", 0},
    {"move", ARG_TYPE_BOTH, "Move file or directory", 0},
    {"paste", ARG_TYPE_ANY, "Paste clipboard contents", 0},
    {"ps", ARG_TYPE_ANY, "List processes", 0},
    {"news", ARG_TYPE_ANY, "Display news", 0},

    // Alias commands - show only alias suggestions
    {"alias", ARG_TYPE_ALIAS, "Define or list aliases", 0},
    {"unalias", ARG_TYPE_ALIAS, "Remove alias", 1},
    {"aliases", ARG_TYPE_ANY, "List all aliases", 0},

    // Bookmark commands - strict matching for bookmark operations
    {"bookmark", ARG_TYPE_DIRECTORY, "Bookmark directories", 0},
    {"bookmarks", ARG_TYPE_ANY, "List all bookmarks", 0},
    {"goto", ARG_TYPE_BOOKMARK, "Jump to a bookmark", 1},
    {"unbookmark", ARG_TYPE_BOOKMARK, "Remove a bookmark", 1},

    // Other utilities
    {"focus_timer", ARG_TYPE_ANY, "Start a focus timer", 0},
    {"weather", ARG_TYPE_FAVORITE_CITY, "Weather information", 1},
    {"grep", ARG_TYPE_FILE, "Search file contents", 0},
    {"grep-text", ARG_TYPE_FILE, "Search text in file", 0},
    {"ripgrep", ARG_TYPE_FILE, "Search with ripgrep", 0},
    {"fzf", ARG_TYPE_ANY, "Fuzzy finder", 0},
    {"clip", ARG_TYPE_ANY, "Clipboard operations", 0},
    {"echo", ARG_TYPE_ANY, "Display text", 0},
    {"theme", ARG_TYPE_THEME, "Shell theme settings", 1},
    {"loc", ARG_TYPE_FILE, "Count lines of code", 0},
    {"git_status", ARG_TYPE_ANY, "Display git status", 0},
    {"gg", ARG_TYPE_ANY, "Git shortcuts", 0},

    // Common external commands
    {"ls", ARG_TYPE_DIRECTORY, "List directory contents", 0},
    {"rm", ARG_TYPE_FILE, "Remove file", 0},
    {"cp", ARG_TYPE_FILE, "Copy file or directory", 0},
    {"mv", ARG_TYPE_BOTH, "Move file or directory", 0},
    {"less", ARG_TYPE_FILE, "View file contents", 0},
    {"more", ARG_TYPE_FILE, "View file contents", 0},
    {"find", ARG_TYPE_DIRECTORY, "Find files", 0},
    {"chmod", ARG_TYPE_FILE, "Change file permissions", 0},
    {"chown", ARG_TYPE_FILE, "Change file owner", 0},
    {"tar", ARG_TYPE_FILE, "Archive utility", 0},
    {"gzip", ARG_TYPE_FILE, "Compress files", 0},
    {"gunzip", ARG_TYPE_FILE, "Decompress files", 0},
    {"zip", ARG_TYPE_FILE, "Compress files", 0},
    {"unzip", ARG_TYPE_FILE, "Decompress files", 0},
    {"bash", ARG_TYPE_FILE, "Run bash script", 0},
    {"sh", ARG_TYPE_FILE, "Run shell script", 0},
    {"python", ARG_TYPE_FILE, "Run Python script", 0},
    {"perl", ARG_TYPE_FILE, "Run Perl script", 0},
    {"java", ARG_TYPE_FILE, "Run Java program", 0},
    {"gcc", ARG_TYPE_FILE, "C compiler", 0},
    {"make", ARG_TYPE_FILE, "Build utility", 0},
    {"diff", ARG_TYPE_FILE, "Compare files", 0},
    {"patch", ARG_TYPE_FILE, "Apply patch file", 0},
    {"man", ARG_TYPE_ANY, "Display manual page", 0},

    {NULL, ARG_TYPE_ANY, NULL, 0} // End marker
};

// Global state
static CommandContext current_context;

void init_tab_completion(void) {
  // Initialize context to defaults
  memset(&current_context, 0, sizeof(CommandContext));
}

void shutdown_tab_completion(void) {
  // Nothing to clean up for now
}

static ArgumentType get_argument_type(const char *cmd, int *strict_match) {
  if (!cmd || !*cmd) {
    if (strict_match)
      *strict_match = 0;
    return ARG_TYPE_ANY;
  }

  for (int i = 0; command_arg_info[i].command != NULL; i++) {
    if (strcmp(command_arg_info[i].command, cmd) == 0) {
      if (strict_match)
        *strict_match = command_arg_info[i].strict_match;
      return command_arg_info[i].arg_type;
    }
  }

  if (strict_match)
    *strict_match = 0;
  return ARG_TYPE_ANY;
}

static void parse_command_context(const char *buffer) {
  // Reset the context
  memset(&current_context, 0, sizeof(CommandContext));

  if (!buffer || !*buffer)
    return;

  // Make a copy of the buffer to tokenize
  char buffer_copy[1024];
  strncpy(buffer_copy, buffer, sizeof(buffer_copy) - 1);
  buffer_copy[sizeof(buffer_copy) - 1] = '\0';

  // Tokenize the buffer
  char *token = strtok(buffer_copy, " \t");
  int token_index = 0;

  // Extract the command if present
  if (token) {
    strncpy(current_context.filter_command, token,
            sizeof(current_context.filter_command) - 1);
    current_context.filter_command[sizeof(current_context.filter_command) - 1] =
        '\0';
    token = strtok(NULL, " \t");
    token_index++;
  }

  // Get the last token (current word being completed)
  char *last_token = NULL;
  while (token) {
    last_token = token;
    token_index++;
    token = strtok(NULL, " \t");
  }

  if (last_token) {
    strncpy(current_context.current_token, last_token,
            sizeof(current_context.current_token) - 1);
    current_context.current_token[sizeof(current_context.current_token) - 1] =
        '\0';
    current_context.token_index =
        token_index - 1; // Adjust token index to be 0-based
  } else {
    // This handles the case where there's only one token (the command)
    // In this case, we're completing the command itself
    strncpy(current_context.current_token, current_context.filter_command,
            sizeof(current_context.current_token) - 1);
    current_context.current_token[sizeof(current_context.current_token) - 1] =
        '\0';
    current_context.token_index = 0;
  }
}

static char *find_path_completions(const char *path) {
  if (!path || !*path)
    return NULL;

  char dir_path[PATH_MAX] = "."; // Default to current directory
  char name_prefix[PATH_MAX] = "";

  // Split path into directory part and name prefix part
  char *last_slash = strrchr(path, '/');
  if (last_slash) {
    // Path contains a directory part
    int dir_len = last_slash - path;
    strncpy(dir_path, path, dir_len);
    dir_path[dir_len] = '\0';

    // Handle absolute path vs. relative path with subdirectories
    if (dir_len == 0) {
      // Handle case where path starts with '/'
      strcpy(dir_path, "/");
    }

    // Extract the name prefix part (after the last slash)
    strncpy(name_prefix, last_slash + 1, sizeof(name_prefix) - 1);
    name_prefix[sizeof(name_prefix) - 1] = '\0';
  } else {
    // No directory part, just a name prefix
    strncpy(name_prefix, path, sizeof(name_prefix) - 1);
    name_prefix[sizeof(name_prefix) - 1] = '\0';
  }

  // Open the directory
  DIR *dir = opendir(dir_path);
  if (!dir) {
    return NULL;
  }

  // Find the first matching entry
  struct dirent *entry;
  char *completion = NULL;
  int name_prefix_len = strlen(name_prefix);

  while ((entry = readdir(dir)) != NULL) {
    // Skip "." and ".." entries always
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    // Check if this entry matches the name prefix
    if (strncasecmp(entry->d_name, name_prefix, name_prefix_len) == 0) {
      // Construct the completion
      char full_path[PATH_MAX];

      // Check if we're completing from root or relative
      if (dir_path[0] == '/' && dir_path[1] == '\0') {
        snprintf(full_path, sizeof(full_path), "/%s", entry->d_name);
      } else if (strcmp(dir_path, ".") == 0) {
        snprintf(full_path, sizeof(full_path), "%s", entry->d_name);
      } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path,
                 entry->d_name);
      }

      // Check if it's a directory, append slash if it is
      struct stat st;
      char path_to_check[PATH_MAX];

      if (full_path[0] == '/') {
        // Absolute path
        strncpy(path_to_check, full_path, sizeof(path_to_check) - 1);
      } else {
        // Relative path, construct with current directory
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd))) {
          snprintf(path_to_check, sizeof(path_to_check), "%s/%s", cwd,
                   full_path);
        } else {
          strncpy(path_to_check, full_path, sizeof(path_to_check) - 1);
        }
      }

      path_to_check[sizeof(path_to_check) - 1] = '\0';

      if (stat(path_to_check, &st) == 0 && S_ISDIR(st.st_mode)) {
        char *dir_completion =
            (char *)malloc(strlen(full_path) + 2); // +2 for slash and null
        if (dir_completion) {
          strcpy(dir_completion, full_path);
          strcat(dir_completion, "/");
          completion = dir_completion;
        }
      } else {
        completion = strdup(full_path);
      }

      break;
    }
  }

  closedir(dir);
  return completion;
}

static char *complete_command(const char *prefix) {
  if (!prefix || !*prefix)
    return NULL;

  // First check builtins
  int builtin_count = lsh_num_builtins();

  for (int i = 0; i < builtin_count; i++) {
    if (strncasecmp(builtin_str[i], prefix, strlen(prefix)) == 0) {
      return strdup(builtin_str[i]);
    }
  }

  // Then check for aliases
  int alias_count;
  char **aliases = get_alias_names(&alias_count);

  if (aliases) {
    for (int i = 0; i < alias_count; i++) {
      if (strncasecmp(aliases[i], prefix, strlen(prefix)) == 0) {
        char *result = strdup(aliases[i]);

        // Free the alias names
        for (int j = 0; j < alias_count; j++) {
          free(aliases[j]);
        }
        free(aliases);

        return result;
      }
    }

    // Free the alias names if we didn't find a match
    for (int i = 0; i < alias_count; i++) {
      free(aliases[i]);
    }
    free(aliases);
  }

  // Finally check for executables in PATH
  char *path = getenv("PATH");
  if (!path)
    return NULL;

  char *path_copy = strdup(path);
  if (!path_copy)
    return NULL;

  char *result = NULL;
  char *dir = strtok(path_copy, ":");

  while (dir && !result) {
    DIR *dirp = opendir(dir);
    if (dirp) {
      struct dirent *entry;
      while ((entry = readdir(dirp)) != NULL) {
        if (strncasecmp(entry->d_name, prefix, strlen(prefix)) == 0) {
          // Check if it's executable
          char full_path[PATH_MAX];
          snprintf(full_path, sizeof(full_path), "%s/%s", dir, entry->d_name);

          struct stat st;
          if (stat(full_path, &st) == 0 && (st.st_mode & S_IXUSR)) {
            result = strdup(entry->d_name);
            break;
          }
        }
      }
      closedir(dirp);
    }
    dir = strtok(NULL, ":");
  }

  free(path_copy);
  return result;
}

static SuggestionList *get_suggestions_by_type(ArgumentType arg_type,
                                               const char *token) {
  if (!token)
    return NULL;

  SuggestionList *suggestions = NULL;
  int matched_count = 0;
  char **items = NULL;

  switch (arg_type) {
  case ARG_TYPE_FILE:
  case ARG_TYPE_DIRECTORY:
  case ARG_TYPE_BOTH: {
    // For files and directories, we'll need a different approach
    // since find_path_completions only returns one match at a time
    char dir_path[PATH_MAX] = "."; // Default to current directory
    char name_prefix[PATH_MAX] = "";

    // Split path into directory part and name prefix part
    char *last_slash = strrchr(token, '/');
    if (last_slash) {
      // Path contains a directory part
      int dir_len = last_slash - token;
      strncpy(dir_path, token, dir_len);
      dir_path[dir_len] = '\0';

      // Handle absolute path vs. relative path with subdirectories
      if (dir_len == 0) {
        // Handle case where path starts with '/'
        strcpy(dir_path, "/");
      }

      // Extract the name prefix part (after the last slash)
      strncpy(name_prefix, last_slash + 1, sizeof(name_prefix) - 1);
      name_prefix[sizeof(name_prefix) - 1] = '\0';
    } else {
      // No directory part, just a name prefix
      strncpy(name_prefix, token, sizeof(name_prefix) - 1);
      name_prefix[sizeof(name_prefix) - 1] = '\0';
    }

    // Open the directory
    DIR *dir = opendir(dir_path);
    if (!dir) {
      return NULL;
    }

    // Count matching entries first
    struct dirent *entry;
    int name_prefix_len = strlen(name_prefix);

    // First count matching entries
    while ((entry = readdir(dir)) != NULL) {
      // Filter based on argument type
      if (arg_type == ARG_TYPE_DIRECTORY || arg_type == ARG_TYPE_FILE) {
        struct stat st;
        char full_path[PATH_MAX];

        // Construct full path to check
        if (strcmp(dir_path, "/") == 0) {
          snprintf(full_path, sizeof(full_path), "/%s", entry->d_name);
        } else {
          snprintf(full_path, sizeof(full_path), "%s/%s", dir_path,
                   entry->d_name);
        }

        if (stat(full_path, &st) == 0) {
          if ((arg_type == ARG_TYPE_DIRECTORY && !S_ISDIR(st.st_mode)) ||
              (arg_type == ARG_TYPE_FILE && S_ISDIR(st.st_mode))) {
            continue; // Skip if doesn't match requested type
          }
        } else {
          continue; // Skip if can't stat
        }
      }

      // Skip "." and ".." entries always
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      // Skip hidden files/dirs unless the prefix starts with a dot
      if (entry->d_name[0] == '.' && name_prefix[0] != '.') {
        continue; // Skip hidden files
      }

      // Check if this entry matches the name prefix
      if (strncasecmp(entry->d_name, name_prefix, name_prefix_len) == 0) {
        matched_count++;
      }
    }

    // Reset directory stream
    rewinddir(dir);

    // Allocate array for matches
    if (matched_count > 0) {
      items = (char **)malloc(matched_count * sizeof(char *));
      if (!items) {
        closedir(dir);
        return NULL;
      }

      // Fill the array with matches
      int idx = 0;
      while ((entry = readdir(dir)) != NULL && idx < matched_count) {
        // Filter based on argument type
        if (arg_type == ARG_TYPE_DIRECTORY || arg_type == ARG_TYPE_FILE) {
          struct stat st;
          char full_path[PATH_MAX];

          // Construct full path to check
          if (strcmp(dir_path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", entry->d_name);
          } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path,
                     entry->d_name);
          }

          if (stat(full_path, &st) == 0) {
            if ((arg_type == ARG_TYPE_DIRECTORY && !S_ISDIR(st.st_mode)) ||
                (arg_type == ARG_TYPE_FILE && S_ISDIR(st.st_mode))) {
              continue; // Skip if doesn't match requested type
            }
          } else {
            continue; // Skip if can't stat
          }
        }

        // Skip "." and ".." entries always
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
          continue;
        }

        // Skip hidden files/dirs unless the prefix starts with a dot
        if (entry->d_name[0] == '.' && name_prefix[0] != '.') {
          continue; // Skip hidden files
        }

        // Check if this entry matches the name prefix
        if (strncasecmp(entry->d_name, name_prefix, name_prefix_len) == 0) {
          // Create suggestion string that includes the directory path if needed
          char *suggestion;

          // Check if it's a directory to add trailing slash
          struct stat st;
          char full_path[PATH_MAX];
          int is_dir = 0;

          // Construct full path to check
          if (strcmp(dir_path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", entry->d_name);
          } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path,
                     entry->d_name);
          }

          if (stat(full_path, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
          }

          // Create suggestion string
          if (last_slash) {
            // Include the directory part in the suggestion

            char dir_name_only[PATH_MAX];
            if (strcmp(dir_path, ".") != 0) {
              char *last_dir_slash = strrchr(dir_path, '/');
              if (last_dir_slash) {
                strcpy(dir_name_only, last_dir_slash + 1);
              } else {
                strcpy(dir_name_only, dir_path);
              }

              if (strcmp(entry->d_name, dir_name_only) == 0) {
                continue;
              }
            }

            char base_path[PATH_MAX];

            // Extract original path up to the last slash
            int base_len = last_slash - token + 1; // +1 to include the slash
            strncpy(base_path, token, base_len);
            base_path[base_len] = '\0';

            // Just return the entry name without the full path
            // since the user already typed the directory part
            char suggestion_path[PATH_MAX];
            if (is_dir) {
              snprintf(suggestion_path, sizeof(suggestion_path), "%s/",
                       entry->d_name);
              suggestion = strdup(suggestion_path);
            } else {
              suggestion = strdup(entry->d_name);
            }
          } else {
            // Just the entry name
            if (is_dir) {
              char suggestion_path[PATH_MAX];
              snprintf(suggestion_path, sizeof(suggestion_path), "%s/",
                       entry->d_name);
              suggestion = strdup(suggestion_path);
            } else {
              suggestion = strdup(entry->d_name);
            }
          }

          if (suggestion) {
            items[idx++] = suggestion;
          }
        }
      }

      // Update actual match count
      matched_count = idx;
    }

    closedir(dir);
    break;
  }

  case ARG_TYPE_BOOKMARK: {
    // Get all bookmarks
    int bookmark_count;
    char **bookmarks = get_bookmark_names(&bookmark_count);

    if (bookmarks) {
      // Count matching bookmarks
      for (int i = 0; i < bookmark_count; i++) {
        if (token[0] == '\0' ||
            strncasecmp(bookmarks[i], token, strlen(token)) == 0) {
          matched_count++;
        }
      }

      // Allocate items array
      if (matched_count > 0) {
        items = (char **)malloc(matched_count * sizeof(char *));
        if (!items) {
          // Free bookmarks
          for (int i = 0; i < bookmark_count; i++) {
            free(bookmarks[i]);
          }
          free(bookmarks);
          return NULL;
        }

        // Fill items array
        int idx = 0;
        for (int i = 0; i < bookmark_count && idx < matched_count; i++) {
          if (token[0] == '\0' ||
              strncasecmp(bookmarks[i], token, strlen(token)) == 0) {
            items[idx++] = strdup(bookmarks[i]);
          }
        }

        // Update actual match count
        matched_count = idx;
      }

      // Free bookmarks
      for (int i = 0; i < bookmark_count; i++) {
        free(bookmarks[i]);
      }
      free(bookmarks);
    }
    break;
  }

  case ARG_TYPE_ALIAS: {
    // Get all aliases
    int alias_count;
    char **aliases = get_alias_names(&alias_count);

    if (aliases) {
      // Count matching aliases
      for (int i = 0; i < alias_count; i++) {
        if (token[0] == '\0' ||
            strncasecmp(aliases[i], token, strlen(token)) == 0) {
          matched_count++;
        }
      }

      // Allocate items array
      if (matched_count > 0) {
        items = (char **)malloc(matched_count * sizeof(char *));
        if (!items) {
          // Free aliases
          for (int i = 0; i < alias_count; i++) {
            free(aliases[i]);
          }
          free(aliases);
          return NULL;
        }

        // Fill items array
        int idx = 0;
        for (int i = 0; i < alias_count && idx < matched_count; i++) {
          if (token[0] == '\0' ||
              strncasecmp(aliases[i], token, strlen(token)) == 0) {
            items[idx++] = strdup(aliases[i]);
          }
        }

        // Update actual match count
        matched_count = idx;
      }

      // Free aliases
      for (int i = 0; i < alias_count; i++) {
        free(aliases[i]);
      }
      free(aliases);
    }
    break;
  }

  case ARG_TYPE_FAVORITE_CITY: {
    // Get all cities
    int city_count;
    char **cities = get_favorite_city_names(&city_count);

    if (cities) {
      // Count matching cities
      for (int i = 0; i < city_count; i++) {
        if (token[0] == '\0' ||
            strncasecmp(cities[i], token, strlen(token)) == 0) {
          matched_count++;
        }
      }

      // Allocate items array
      if (matched_count > 0) {
        items = (char **)malloc(matched_count * sizeof(char *));
        if (!items) {
          // Free cities
          for (int i = 0; i < city_count; i++) {
            free(cities[i]);
          }
          free(cities);
          return NULL;
        }

        // Fill items array
        int idx = 0;
        for (int i = 0; i < city_count && idx < matched_count; i++) {
          if (token[0] == '\0' ||
              strncasecmp(cities[i], token, strlen(token)) == 0) {
            items[idx++] = strdup(cities[i]);
          }
        }

        // Update actual match count
        matched_count = idx;
      }

      // Free cities
      for (int i = 0; i < city_count; i++) {
        free(cities[i]);
      }
      free(cities);
    }
    break;
  }

  case ARG_TYPE_THEME: {
    // Get all themes
    int theme_count;
    char **themes = get_theme_names(&theme_count);

    if (themes) {
      // Count matching themes
      for (int i = 0; i < theme_count; i++) {
        if (token[0] == '\0' ||
            strncasecmp(themes[i], token, strlen(token)) == 0) {
          matched_count++;
        }
      }

      // Allocate items array
      if (matched_count > 0) {
        items = (char **)malloc(matched_count * sizeof(char *));
        if (!items) {
          // Free themes
          for (int i = 0; i < theme_count; i++) {
            free(themes[i]);
          }
          free(themes);
          return NULL;
        }

        // Fill items array
        int idx = 0;
        for (int i = 0; i < theme_count && idx < matched_count; i++) {
          if (token[0] == '\0' ||
              strncasecmp(themes[i], token, strlen(token)) == 0) {
            items[idx++] = strdup(themes[i]);
          }
        }

        // Update actual match count
        matched_count = idx;
      }

      // Free themes
      for (int i = 0; i < theme_count; i++) {
        free(themes[i]);
      }
      free(themes);
    }
    break;
  }

  case ARG_TYPE_COMMAND: {
    // get all available commands
    int builtin_count = lsh_num_builtins();

    // count matching commands

    for (int i = 0; i < builtin_count; i++) {
      if (token[0] == '\0' ||
          strncasecmp(builtin_str[i], token, strlen(token)) == 0) {
        matched_count++;
      }
    }
    // allocate items array

    if (matched_count > 0) {
      items = (char **)malloc(matched_count * sizeof(char *));
      if (!items) {
        return NULL;
      }
      // fill items array
      int idx = 0;
      for (int i = 0; i < builtin_count && idx < matched_count; i++) {
        if (token[0] == '\0' ||
            strncasecmp(builtin_str[i], token, strlen(token)) == 0) {
          items[idx++] = strdup(builtin_str[i]);
        }
      }

      matched_count = idx;
    }
    break;
  }

  case ARG_TYPE_ANY:
  default:
    // For ARG_TYPE_ANY, we'll use path completions similar to FILE/DIRECTORY
    // Re-use the FILE/DIRECTORY case by setting arg_type and falling through
    arg_type = ARG_TYPE_BOTH;
    goto case_label_file_dir;
  }

case_label_file_dir:

  // Create and return the suggestion list
  if (matched_count > 0 && items) {
    suggestions = (SuggestionList *)malloc(sizeof(SuggestionList));
    if (suggestions) {
      suggestions->items = items;
      suggestions->count = matched_count;
      suggestions->current_index = 0;
    } else {
      // Free items if we couldn't allocate the suggestions structure
      for (int i = 0; i < matched_count; i++) {
        free(items[i]);
      }
      free(items);
    }
  }

  return suggestions;
}

static char *complete_argument_by_type(ArgumentType arg_type,
                                       const char *token) {
  if (!token)
    return NULL;

  // Get all suggestions
  SuggestionList *suggestions = get_suggestions_by_type(arg_type, token);

  if (suggestions && suggestions->count > 0) {
    // Get the first suggestion
    char *result = strdup(suggestions->items[0]);

    // Free the suggestion list
    free_suggestion_list(suggestions);

    return result;
  }

  return NULL;
}

void free_suggestion_list(SuggestionList *list) {
  if (!list)
    return;

  if (list->items) {
    for (int i = 0; i < list->count; i++) {
      if (list->items[i]) {
        free(list->items[i]);
      }
    }
    free(list->items);
  }

  free(list);
}

SuggestionList *get_suggestion_list(const char *buffer, const char *prefix) {
  if (!buffer)
    return NULL;

  // Parse the command context
  parse_command_context(buffer);

  // Support cycling through builtins/commands when at the start of the line
  if (current_context.token_index == 0) {
    // When completing a command, get all matching commands
    int matched_count = 0;
    char **items = NULL;

    // Count builtins that match the prefix
    for (int i = 0; i < lsh_num_builtins(); i++) {
      if (prefix == NULL || prefix[0] == '\0' ||
          strncasecmp(builtin_str[i], prefix, strlen(prefix)) == 0) {
        matched_count++;
      }
    }

    // We'll collect up to 100 matching commands
    if (matched_count > 0) {
      items = (char **)malloc(matched_count * sizeof(char *));
      if (!items) {
        return NULL;
      }

      // Fill with matching commands
      int idx = 0;
      for (int i = 0; i < lsh_num_builtins() && idx < matched_count; i++) {
        if (prefix == NULL || prefix[0] == '\0' ||
            strncasecmp(builtin_str[i], prefix, strlen(prefix)) == 0) {
          items[idx++] = strdup(builtin_str[i]);
        }
      }

      // Update actual match count
      matched_count = idx;

      // Create and return suggestion list
      SuggestionList *suggestions =
          (SuggestionList *)malloc(sizeof(SuggestionList));
      if (suggestions) {
        suggestions->items = items;
        suggestions->count = matched_count;
        suggestions->current_index = 0;
        return suggestions;
      } else {
        // Free items if we couldn't allocate the suggestions structure
        for (int i = 0; i < matched_count; i++) {
          free(items[i]);
        }
        free(items);
      }
    }

    return NULL;
  }

  // For subsequent tokens, look at the command to determine what to complete
  int strict_match = 0;
  ArgumentType arg_type =
      get_argument_type(current_context.filter_command, &strict_match);

  // Get suggestions for the expected argument type
  SuggestionList *suggestions = get_suggestions_by_type(
      arg_type, prefix ? prefix : current_context.current_token);

  // If we got suggestions or we're in strict mode, return them
  if ((suggestions && suggestions->count > 0) || strict_match) {
    return suggestions;
  }

  // If we're not in strict mode and didn't find suggestions of the expected
  // type, try to fall back to path completions for most types (if they're
  // different)
  if (arg_type != ARG_TYPE_FILE && arg_type != ARG_TYPE_DIRECTORY &&
      arg_type != ARG_TYPE_BOTH) {
    // Free the empty suggestions list if any
    if (suggestions) {
      free_suggestion_list(suggestions);
    }

    // Try path completions as a fallback
    return get_suggestions_by_type(
        ARG_TYPE_BOTH, prefix ? prefix : current_context.current_token);
  }

  return suggestions;
}

char *get_tab_completion(const char *buffer) {
  if (!buffer)
    return NULL;

  // Parse the command context
  parse_command_context(buffer);

  // If we're completing the first token, it's a command
  if (current_context.token_index == 0) {
    return complete_command(current_context.current_token);
  }

  // For subsequent tokens, look at the command to determine what to complete
  int strict_match = 0;
  ArgumentType arg_type =
      get_argument_type(current_context.filter_command, &strict_match);

  // Try to get completion for the expected argument type
  char *completion =
      complete_argument_by_type(arg_type, current_context.current_token);

  // If we got a completion or we're in strict mode, return the result
  if (completion || strict_match) {
    return completion;
  }

  // If we're not in strict mode and didn't find a match of the expected type,
  // try to fall back to path completions for most types
  if (arg_type != ARG_TYPE_FILE && arg_type != ARG_TYPE_DIRECTORY &&
      arg_type != ARG_TYPE_BOTH) {
    completion = find_path_completions(current_context.current_token);
  }

  return completion;
}
