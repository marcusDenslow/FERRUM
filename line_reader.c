/**
 * line_reader.c
 * Implementation of line reading and parsing with context-aware suggestions
 */

#include "line_reader.h"
#include "aliases.h"
#include "bookmarks.h" // Added for bookmark support
#include "builtins.h"  // Added for history access
#include "common.h"
#include "git_integration.h"
#include "persistent_history.h"
#include "tab_complete.h"
#include "themes.h"
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Suggestion state - make these global
static int has_suggestion = 0;
static int suggestion_index = 0;
static char **suggestions = NULL;
static int suggestion_count = 0;
static char full_suggestion[LSH_RL_BUFSIZE] = {0};
static int prefix_start = 0;
static int menu_mode = 0;
static int menu_start_line = 0;

// Define colors for suggestions
#define SUGGESTION_COLOR "\033[2;37m"  // Dim white color for suggestions
#define HIGHLIGHT_COLOR "\033[7;36m"   // Highlighted background for selected item
#define NORMAL_COLOR "\033[0;36m"      // Normal color for menu items
#define RESET_COLOR "\033[0m"

/**
 * Check if a command is valid
 *
 * @param cmd The command to check
 * @return 1 if valid, 0 if not
 */
int is_valid_command(const char *cmd) {
  if (!cmd || cmd[0] == '\0') {
    return 0; // Empty command is not valid
  }

  // Extract just the command part (before any spaces)
  char command_part[LSH_RL_BUFSIZE];
  int i = 0;
  while (cmd[i] && !isspace(cmd[i]) && i < LSH_RL_BUFSIZE - 1) {
    command_part[i] = cmd[i];
    i++;
  }
  command_part[i] = '\0';

  // Check built-in commands
  for (int i = 0; i < lsh_num_builtins(); i++) {
    if (strcasecmp(command_part, builtin_str[i]) == 0) {
      return 1;
    }
  }

  // Check aliases
  AliasEntry *alias = find_alias(command_part);
  if (alias) {
    return 1;
  }

  void generate_enhanced_prompt(char *prompt_buffer, size_t buffer_size) {
    // Get current directory information
    char cwd[PATH_MAX];
    char parent_dir[PATH_MAX / 2];
    char current_dir[PATH_MAX / 2];

    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        get_path_display(cwd, parent_dir, current_dir, PATH_MAX / 2);
    } else {
        strcpy(parent_dir, "unknown");
        strcpy(current_dir, "dir");
    }

    // Get Git information - modified to extract just the branch name
    char git_display[LSH_RL_BUFSIZE] = {0};
    char *git_status_info = get_git_status();
    if (git_status_info != NULL) {
        // Extract just the branch name from git status info
        char branch_name[LSH_RL_BUFSIZE] = {0};
        char *paren_open = strchr(git_status_info, '(');
        char *paren_close = strchr(git_status_info, ')');
        
        if (paren_open && paren_close && paren_close > paren_open) {
            // Extract content between parentheses - should be the branch name
            size_t branch_len = paren_close - paren_open - 1;
            if (branch_len < sizeof(branch_name)) {
                strncpy(branch_name, paren_open + 1, branch_len);
                branch_name[branch_len] = '\0';
                snprintf(git_display, sizeof(git_display), " \033[1;35mgit:(%s)\033[0m", branch_name);
            } else {
                // Fallback if branch name is too long
                snprintf(git_display, sizeof(git_display), " \033[1;35mgit:(?)\033[0m");
            }
        } else {
            // If we can't parse the branch name, just use the whole status
            snprintf(git_display, sizeof(git_display), " \033[1;35mgit:(%s)\033[0m", git_status_info);
        }
        
        free(git_status_info);
    }

    // Format the prompt
    snprintf(prompt_buffer, buffer_size, "\033[1;36m%s/%s\033[0m%s \033[1;31m✗\033[0m ", 
             parent_dir, current_dir, git_display);
}

  // Check if it's an executable in PATH
  // We'll use a simplified approach - check if the file exists and is executable
  struct stat st;
  char path_buffer[PATH_MAX];

  // First check if it's an absolute or relative path
  if (strchr(command_part, '/')) {
    // Path contains slashes, check if file exists and is executable
    if (stat(command_part, &st) == 0 && (st.st_mode & S_IXUSR)) {
      return 1;
    }
  } else {
    // Check in current directory
    snprintf(path_buffer, PATH_MAX, "./%s", command_part);
    if (stat(path_buffer, &st) == 0 && (st.st_mode & S_IXUSR)) {
      return 1;
    }

    // Check in PATH directories
    char *path_env = getenv("PATH");
    if (!path_env) {
      return 0; // No PATH defined
    }

    char *path_copy = strdup(path_env);
    if (!path_copy) {
      return 0; // Memory allocation error
    }

    char *token, *rest = path_copy;
    while ((token = strtok_r(rest, ":", &rest))) {
      // Construct full path to check
      snprintf(path_buffer, PATH_MAX, "%s/%s", token, command_part);
      if (stat(path_buffer, &st) == 0 && (st.st_mode & S_IXUSR)) {
        free(path_copy);
        return 1;
      }
    }
    free(path_copy);
  }

  return 0; // Command not found
}

/**
 * Read a key from the terminal
 * 
 * Handles special keys and escape sequences
 */
int read_key(void) {
  unsigned char c;
  int nread;
  char seq[6];
  
  // Define local constants for special keys 
  #define LOCAL_KEY_SHIFT_ENTER 1010
  
  // Read a character
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      return -1;
  }
  
  // Handle carriage return (CR) as enter
  if (c == 13) {
    return KEY_ENTER;
  }
  
  // Handle escape sequences for arrow keys and other special keys
  if (c == KEY_ESCAPE) {
    // Read up to 5 additional chars
    int i = 0;
    fd_set readfds;
    struct timeval timeout;
    
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = 50000; // 50ms timeout
    
    // Try to read the sequence with timeout to avoid blocking
    while (i < 5) {
      int select_result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
      if (select_result <= 0) break; // Timeout or error
      
      if (read(STDIN_FILENO, &seq[i], 1) != 1) break;
      i++;
      
      // Check for known sequences
      if (i >= 2 && seq[0] == '[') {
        switch (seq[1]) {
          case 'A': return KEY_UP;
          case 'B': return KEY_DOWN;
          case 'C': return KEY_RIGHT;
          case 'D': return KEY_LEFT;
        }
      }
      
      // Check for Shift+Enter sequence (varies by terminal, try common forms)
      if (i >= 3) {
        // xterm/vte: ESC [ 13 ; 2 ~
        if (seq[0] == '[' && seq[1] == '1' && seq[2] == '3' && i >= 5 && seq[3] == ';' && seq[4] == '2') {
          return LOCAL_KEY_SHIFT_ENTER;
        }
        
        // Another common sequence for Shift+Enter in some terminals
        if (seq[0] == 'O' && seq[1] == '2' && seq[2] == 'M') {
          return LOCAL_KEY_SHIFT_ENTER;
        }
      }
    }
    
    return KEY_ESCAPE;
  }
  
  return c;
}

/**
 * Update suggestions based on current input
 */
void update_suggestions(const char *buffer, int position) {
  // Free previous suggestions if any
  if (suggestions) {
    for (int i = 0; i < suggestion_count; i++) {
      if (suggestions[i]) free(suggestions[i]);
    }
    free(suggestions);
    suggestions = NULL;
    suggestion_count = 0;
  }
  
  has_suggestion = 0;
  
  // Parse command line
  prefix_start = 0;
  
  // Find the last space to determine where the prefix starts
  char *last_space = strrchr(buffer, ' ');
  if (last_space) {
    // We have a command and partial argument
    prefix_start = (last_space - buffer) + 1;
    
    // Skip additional spaces
    while (buffer[prefix_start] == ' ' && buffer[prefix_start] != '\0') {
      prefix_start++;
    }
  } else {
    // Just a command, no arguments
    prefix_start = 0;
  }
  
  // Build list of suggestions
  suggestions = NULL;
  suggestion_count = 0;
  
  // Get command or file suggestions based on context
  if (!last_space) {
    // Command suggestions
    // First count how many we'll need
    for (int i = 0; i < lsh_num_builtins(); i++) {
      if (strncasecmp(builtin_str[i], buffer, strlen(buffer)) == 0) {
        suggestion_count++;
      }
    }
    
    // Add alias count - rough estimate
    int alias_count;
    char **aliases = get_alias_names(&alias_count);
    if (aliases) {
      for (int i = 0; i < alias_count; i++) {
        if (strncasecmp(aliases[i], buffer, strlen(buffer)) == 0) {
          suggestion_count++;
        }
      }
      
      // Free alias names, we'll get them again
      for (int i = 0; i < alias_count; i++) {
        free(aliases[i]);
      }
      free(aliases);
    }
    
    // Allocate suggestions array
    if (suggestion_count > 0) {
      suggestions = (char**)malloc(suggestion_count * sizeof(char*));
      if (!suggestions) {
        fprintf(stderr, "Memory allocation error\n");
        suggestion_count = 0;
      } else {
        // Initialize all entries to NULL
        for (int i = 0; i < suggestion_count; i++) {
          suggestions[i] = NULL;
        }
      }
    }
    
    // Now fill the suggestions array
    int idx = 0;
    
    // Add builtin commands
    for (int i = 0; i < lsh_num_builtins() && idx < suggestion_count; i++) {
      if (strncasecmp(builtin_str[i], buffer, strlen(buffer)) == 0) {
        suggestions[idx++] = strdup(builtin_str[i]);
      }
    }
    
    // Add aliases
    aliases = get_alias_names(&alias_count);
    if (aliases) {
      for (int i = 0; i < alias_count && idx < suggestion_count; i++) {
        if (strncasecmp(aliases[i], buffer, strlen(buffer)) == 0) {
          suggestions[idx++] = strdup(aliases[i]);
        }
      }
      
      // Free alias names
      for (int i = 0; i < alias_count; i++) {
        free(aliases[i]);
      }
      free(aliases);
    }
    
    // Update actual suggestion count in case we got fewer than expected
    suggestion_count = idx;
  } else {
    // File/directory suggestions
    char prefix[PATH_MAX] = "";
    char dir_path[PATH_MAX] = ".";
    char name_prefix[PATH_MAX] = "";
    
    if (buffer[prefix_start] != '\0') {
      strncpy(prefix, &buffer[prefix_start], sizeof(prefix) - 1);
      prefix[sizeof(prefix) - 1] = '\0';
      
      // Split path into directory part and name prefix part
      char *last_slash = strrchr(prefix, '/');
      if (last_slash) {
        // Path contains a directory part
        int dir_len = last_slash - prefix;
        strncpy(dir_path, prefix, dir_len);
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
        strncpy(name_prefix, prefix, sizeof(name_prefix) - 1);
        name_prefix[sizeof(name_prefix) - 1] = '\0';
      }
    }
    
    // Count matching files and directories
    DIR *dir = opendir(dir_path);
    if (dir) {
      struct dirent *entry;
      
      // First count the number of matching entries
      while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".." if there's a name prefix
        if (name_prefix[0] != '\0' && 
            (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)) {
          continue;
        }
        
        if (strncasecmp(entry->d_name, name_prefix, strlen(name_prefix)) == 0) {
          suggestion_count++;
        }
      }
      
      rewinddir(dir);
      
      // Allocate suggestions array
      if (suggestion_count > 0) {
        suggestions = (char**)malloc(suggestion_count * sizeof(char*));
        if (!suggestions) {
          fprintf(stderr, "Memory allocation error\n");
          suggestion_count = 0;
        } else {
          // Initialize all entries to NULL
          for (int i = 0; i < suggestion_count; i++) {
            suggestions[i] = NULL;
          }
        }
      }
      
      // Fill the suggestions array
      int idx = 0;
      while ((entry = readdir(dir)) != NULL && idx < suggestion_count) {
        // Skip "." and ".." if there's a name prefix
        if (name_prefix[0] != '\0' && 
            (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)) {
          continue;
        }
        
        if (strncasecmp(entry->d_name, name_prefix, strlen(name_prefix)) == 0) {
          // Create the full path suggestion
          char full_path[PATH_MAX * 2];
          
          // For subdirectory paths, just store the entry name
          // (we already handled path splitting earlier in this function)
          strncpy(full_path, entry->d_name, sizeof(full_path) - 1);
          
          // Check if it's a directory and add trailing slash if needed
          struct stat st;
          char check_path[PATH_MAX * 2];
          
          if (dir_path[0] == '/') {
            // Absolute path
            snprintf(check_path, sizeof(check_path) - 1, "%s/%s", dir_path, entry->d_name);
          } else {
            // Relative path
            snprintf(check_path, sizeof(check_path) - 1, "%s/%s", dir_path, entry->d_name);
          }
          
          if (stat(check_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            strncat(full_path, "/", sizeof(full_path) - strlen(full_path) - 1);
          }
          
          suggestions[idx++] = strdup(full_path);
        }
      }
      
      // Update actual suggestion count
      suggestion_count = idx;
      
      closedir(dir);
    }
  }
  
  // If we have suggestions, use the first one
  if (suggestion_count > 0) {
    // Skip showing "./" suggestions as they're often not useful
    if (strcmp(suggestions[0], "./") == 0) {
      has_suggestion = 0;
    } else {
      has_suggestion = 1;
      suggestion_index = 0;
      
      // Create the full suggestion string that would be accepted
      if (prefix_start > 0) {
        // We're completing an argument
        strncpy(full_suggestion, buffer, prefix_start);
        full_suggestion[prefix_start] = '\0';
        strncat(full_suggestion, suggestions[suggestion_index], 
                sizeof(full_suggestion) - strlen(full_suggestion) - 1);
      } else {
        // We're completing a command
        strncpy(full_suggestion, suggestions[suggestion_index], sizeof(full_suggestion) - 1);
      }
      full_suggestion[sizeof(full_suggestion) - 1] = '\0';
    }
  }
}

/**
 * Display a single inline suggestion (when not in menu mode)
 */
void display_inline_suggestion(const char *prompt_buffer, const char *buffer, int position) {
  if (has_suggestion && suggestion_count > 0) {
    // Determine what part should be in normal text and what part in suggestion color
    char current_text[LSH_RL_BUFSIZE] = "";
    char suggestion_text[LSH_RL_BUFSIZE] = "";
    
    strncpy(current_text, buffer, position);
    current_text[position] = '\0';
    
    // Extract just the suggestion part (after what user typed)
    int current_len = 0;
    
    if (prefix_start > 0) {
      char current_arg[LSH_RL_BUFSIZE] = "";
      strncpy(current_arg, &buffer[prefix_start], position - prefix_start);
      current_arg[position - prefix_start] = '\0';
      current_len = strlen(current_arg);
      
      if (current_len > 0) {
        // Handle nested paths with subdirectories
        char *last_slash = strrchr(current_arg, '/');
        if (last_slash) {
          // We're in a subdirectory path
          char *filename_part = last_slash + 1;
          int filename_len = strlen(filename_part);
          
          // When dealing with a path like "cd shelltest/ha", we need to match "ha" against
          // the entry in the suggestions array (which would be e.g. "halla")
          
          // Check if the filename part is a prefix of the suggestions entry
          if (strncasecmp(suggestions[suggestion_index], filename_part, filename_len) == 0) {
            // Copy the suggestion text starting from where the user input ends
            strncpy(suggestion_text, suggestions[suggestion_index] + filename_len, 
                    sizeof(suggestion_text) - 1);
          }
        } else {
          // Regular path (no subdirectories)
          // Find where the current argument ends in the suggestion
          char *suggestion_part = suggestions[suggestion_index];
          if (strncasecmp(suggestion_part, current_arg, current_len) == 0) {
            strncpy(suggestion_text, suggestion_part + current_len, 
                    sizeof(suggestion_text) - 1);
          }
        }
      } else {
        // Use the entire suggestion
        strncpy(suggestion_text, suggestions[suggestion_index], 
              sizeof(suggestion_text) - 1);
      }
    } else {
      // Completing a command
      if (strlen(buffer) <= strlen(suggestions[suggestion_index]) &&
          strncasecmp(suggestions[suggestion_index], buffer, strlen(buffer)) == 0) {
        strncpy(suggestion_text, 
              suggestions[suggestion_index] + strlen(buffer),
              sizeof(suggestion_text) - 1);
      }
    }
    
    // If there's no suggestion text (exact match), then don't show suggestion
    if (strlen(suggestion_text) == 0) {
      // Clear the current line
      printf("\r\033[K");
      
      // Display prompt and current text without suggestion
      printf("%s%s", prompt_buffer, buffer);
      fflush(stdout);
    } else {
      // Clear the current line
      printf("\r\033[K");
      
      // Display prompt and current text
      printf("%s%s", prompt_buffer, current_text);
      
      // Display the suggestion part in dim color
      printf("%s%s%s", SUGGESTION_COLOR, suggestion_text, RESET_COLOR);
      
      // Move cursor back to end of actual input
      int suggestion_len = strlen(suggestion_text);
      for (int i = 0; i < suggestion_len; i++) {
        printf("\b");
      }
      
      fflush(stdout);
    }
  } else {
    // No suggestions, just redraw the current line
    printf("\r\033[K%s%s", prompt_buffer, buffer);
    fflush(stdout);
  }
}

/**
 * Function to clear the menu
 */
void clear_menu() {
  if (menu_start_line > 0) {
    // Save current cursor position
    printf("\033[s");
    
    // Move cursor to where the menu starts (one line below input)
    printf("\033[1B\r");
    
    // Clear each line of the menu
    for (int i = 0; i < menu_start_line; i++) {
      printf("\033[K");  // Clear entire line
      if (i < menu_start_line - 1) {
        printf("\033[1B\r");  // Move down to next line if not the last
      }
    }
    
    // Restore cursor position (back to input line)
    printf("\033[u");
    fflush(stdout);
    
    menu_start_line = 0;
  }
}


/**
 * Function to display menu of suggestions
 */

void display_menu(const char *prompt_buffer, const char *buffer, int position) {
  if (!has_suggestion || suggestion_count == 0) {
    // No suggestions to show
    return;
  }
  
  // First, clear any existing menu
  clear_menu();
  
  // Save current cursor position (should be at end of input)
  printf("\033[s");
  
  // Calculate how many items to show
  int show_count = suggestion_count;
  if (show_count > 10) show_count = 10;  // Max 10 items in menu
  
  // Move to the beginning of the next line (below input)
  printf("\n\r");
  
  // Display each suggestion on a line
  for (int i = 0; i < show_count; i++) {
    if (i > 0) {
      printf("\n\r");  // New line for each suggestion after the first
    }
    
    if (i == suggestion_index) {
      // Highlight the selected item
      printf("%s%s%s", HIGHLIGHT_COLOR, suggestions[i], RESET_COLOR);
    } else {
      // Normal color for non-selected items
      printf("%s%s%s", NORMAL_COLOR, suggestions[i], RESET_COLOR);
    }
  }
  
  // Store number of menu lines displayed
  menu_start_line = show_count;
  
  // Restore cursor to original position (back to input line)
  printf("\033[u");
  fflush(stdout);
}


/**
 * Function to refresh the display - based on menu mode
 */

void refresh_display(const char *prompt_buffer, const char *buffer, int position) {
  // Always clear any existing menu first
  clear_menu();
  
  // Always show inline suggestion - even in menu mode
  display_inline_suggestion(prompt_buffer, buffer, position);
  
  if (menu_mode) {
    // Also show menu of options below the input line
    display_menu(prompt_buffer, buffer, position);
  }
}

/**
 * Read a line of input from the user
 *
 * @return The line read from stdin
 */
char *lsh_read_line(void) {
  int bufsize = LSH_RL_BUFSIZE;
  int position = 0;
  char *buffer = malloc(sizeof(char) * bufsize);
  int c;
  int history_position = -1;
  
  // Prompt buffer for enhanced prompt
  char prompt_buffer[LSH_RL_BUFSIZE];
  
  // Reset menu mode
  menu_mode = 0;
  menu_start_line = 0;
  
  if (!buffer) {
    fprintf(stderr, "lsh: allocation error\n");
    exit(EXIT_FAILURE);
  }
  
  // Clear the buffer
  buffer[0] = '\0';
  
  // Generate enhanced prompt - INLINED CODE
  {
    // Get current directory information
    char cwd[PATH_MAX];
    char parent_dir[PATH_MAX / 2];
    char current_dir[PATH_MAX / 2];

    if (getcwd(cwd, sizeof(cwd)) != NULL) {
      get_path_display(cwd, parent_dir, current_dir, PATH_MAX / 2);
    } else {
      strcpy(parent_dir, "unknown");
      strcpy(current_dir, "dir");
    }

    // Get Git information
    char git_display[LSH_RL_BUFSIZE] = {0};
    char *git_status_info = get_git_status();
    if (git_status_info != NULL) {
      // Extract just the branch name from git status info
      char branch_name[LSH_RL_BUFSIZE] = {0};
      char *paren_open = strchr(git_status_info, '(');
      char *paren_close = strchr(git_status_info, ')');
      
      if (paren_open && paren_close && paren_close > paren_open) {
        // Extract content between parentheses - should be the branch name
        size_t branch_len = paren_close - paren_open - 1;
        if (branch_len < sizeof(branch_name)) {
          strncpy(branch_name, paren_open + 1, branch_len);
          branch_name[branch_len] = '\0';
          snprintf(git_display, sizeof(git_display), " \033[1;35mgit:(%s)\033[0m", branch_name);
        } else {
          // Fallback if branch name is too long
          snprintf(git_display, sizeof(git_display), " \033[1;35mgit:(?)\033[0m");
        }
      } else {
        // If we can't parse the branch name, just use the whole status
        snprintf(git_display, sizeof(git_display), " \033[1;35mgit:(%s)\033[0m", git_status_info);
      }
      
      free(git_status_info);
    }

    // Format the prompt
    snprintf(prompt_buffer, sizeof(prompt_buffer), "\033[1;36m%s/%s\033[0m%s \033[1;31m✗\033[0m ", 
             parent_dir, current_dir, git_display);
  }
  
  // Display prompt
  printf("%s", prompt_buffer);
  fflush(stdout);
  
  // Initialize suggestions
  update_suggestions(buffer, position);

  while (1) {
    c = read_key();
    
    if (c == KEY_ENTER || c == '\n' || c == '\r') {
      if (menu_mode) {
        // In menu mode: accept the highlighted suggestion without executing
        if (has_suggestion && suggestion_count > 0) {
          // Update buffer with selected suggestion
          if (prefix_start > 0) {
            // We're completing an argument
            char temp[LSH_RL_BUFSIZE];
            char path_part[LSH_RL_BUFSIZE] = "";
            
            // Extract the existing path part
            strncpy(path_part, &buffer[prefix_start], position - prefix_start);
            path_part[position - prefix_start] = '\0';
            
            // Find last slash in the existing path_part
            char *last_slash = strrchr(path_part, '/');
            
            if (last_slash) {
              // Get the directory part up to the last slash (including the slash)
              int dir_part_len = (last_slash - path_part) + 1;
              char dir_part[LSH_RL_BUFSIZE] = "";
              strncpy(dir_part, path_part, dir_part_len);
              dir_part[dir_part_len] = '\0';
              
              // Construct the complete path by keeping the command and directory part
              strncpy(temp, buffer, prefix_start);
              temp[prefix_start] = '\0';
              strncat(temp, dir_part, LSH_RL_BUFSIZE - strlen(temp) - 1);
              strncat(temp, suggestions[suggestion_index], LSH_RL_BUFSIZE - strlen(temp) - 1);
            } else {
              // No slash in current argument, normal behavior
              strncpy(temp, buffer, prefix_start);
              temp[prefix_start] = '\0';
              strncat(temp, suggestions[suggestion_index], LSH_RL_BUFSIZE - strlen(temp) - 1);
            }
            
            strncpy(buffer, temp, bufsize - 1);
          } else {
            // We're completing a command
            strncpy(buffer, suggestions[suggestion_index], bufsize - 1);
          }
          buffer[bufsize - 1] = '\0';
          position = strlen(buffer);
          
          // Clear the menu
          clear_menu();
          menu_mode = 0;
          
          // Redraw the line with accepted suggestion
          printf("\r\033[K%s%s", prompt_buffer, buffer);
          fflush(stdout);
          
          // Check if the accepted suggestion is a directory
          if (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == '/') {
            // It's a directory, update suggestions to show its contents
            update_suggestions(buffer, position);
            display_inline_suggestion(prompt_buffer, buffer, position);
          }
          
          // Update suggestions after accepting the directory suggestion
          update_suggestions(buffer, position);
          display_inline_suggestion(prompt_buffer, buffer, position);
        }
      } else {
        // Not in menu mode: execute the command as is
        buffer[position] = '\0';
        printf("\n");
        fflush(stdout);
        break;
      }
    } else if (c == KEY_ESCAPE) {
      if (menu_mode) {
        // Exit menu mode
        menu_mode = 0;
        clear_menu();
        display_inline_suggestion(prompt_buffer, buffer, position);
      }
    } else if (c == KEY_BACKSPACE || c == 127) {
      // Handle backspace
      if (position > 0) {
        position--;
        buffer[position] = '\0';
        
        // Exit menu mode when backspacing
        if (menu_mode) {
          menu_mode = 0;
          clear_menu();
        }
        
        // Update suggestions after backspace
        update_suggestions(buffer, position);
        display_inline_suggestion(prompt_buffer, buffer, position);
      }
    } else if (c == KEY_TAB) {
      if (menu_mode) {
        // Already in menu mode: cycle to next suggestion
        if (suggestion_count > 0) {
          suggestion_index = (suggestion_index + 1) % suggestion_count;
          
          // Update full_suggestion with the newly selected suggestion
          if (prefix_start > 0) {
            // We're completing an argument
            char path_part[LSH_RL_BUFSIZE] = "";
            
            // Extract the existing path part
            strncpy(path_part, &buffer[prefix_start], position - prefix_start);
            path_part[position - prefix_start] = '\0';
            
            // Find last slash in the existing path_part
            char *last_slash = strrchr(path_part, '/');
            
            if (last_slash) {
              // Get the directory part up to the last slash (including the slash)
              int dir_part_len = (last_slash - path_part) + 1;
              char dir_part[LSH_RL_BUFSIZE] = "";
              strncpy(dir_part, path_part, dir_part_len);
              dir_part[dir_part_len] = '\0';
              
              // Construct the complete path by keeping the command and directory part
              strncpy(full_suggestion, buffer, prefix_start);
              full_suggestion[prefix_start] = '\0';
              strncat(full_suggestion, dir_part, sizeof(full_suggestion) - strlen(full_suggestion) - 1);
              strncat(full_suggestion, suggestions[suggestion_index], sizeof(full_suggestion) - strlen(full_suggestion) - 1);
            } else {
              // No slash in current argument, normal behavior
              strncpy(full_suggestion, buffer, prefix_start);
              full_suggestion[prefix_start] = '\0';
              strncat(full_suggestion, suggestions[suggestion_index], sizeof(full_suggestion) - strlen(full_suggestion) - 1);
            }
          } else {
            // We're completing a command
            strncpy(full_suggestion, suggestions[suggestion_index], sizeof(full_suggestion) - 1);
          }
          full_suggestion[sizeof(full_suggestion) - 1] = '\0';
          
          refresh_display(prompt_buffer, buffer, position);
        }
      } else {
        // If there's only one suggestion, accept it directly
        if (has_suggestion && suggestion_count == 1) {
          // Update buffer with the single suggestion
          if (prefix_start > 0) {
            // We're completing an argument
            char temp[LSH_RL_BUFSIZE];
            char path_part[LSH_RL_BUFSIZE] = "";
            
            // Extract the existing path part
            strncpy(path_part, &buffer[prefix_start], position - prefix_start);
            path_part[position - prefix_start] = '\0';
            
            // Find last slash in the existing path_part
            char *last_slash = strrchr(path_part, '/');
            
            if (last_slash) {
              // Get the directory part up to the last slash (including the slash)
              int dir_part_len = (last_slash - path_part) + 1;
              char dir_part[LSH_RL_BUFSIZE] = "";
              strncpy(dir_part, path_part, dir_part_len);
              dir_part[dir_part_len] = '\0';
              
              // Construct the complete path by keeping the command and directory part
              strncpy(temp, buffer, prefix_start);
              temp[prefix_start] = '\0';
              strncat(temp, dir_part, LSH_RL_BUFSIZE - strlen(temp) - 1);
              strncat(temp, suggestions[suggestion_index], LSH_RL_BUFSIZE - strlen(temp) - 1);
            } else {
              // No slash in current argument, normal behavior
              strncpy(temp, buffer, prefix_start);
              temp[prefix_start] = '\0';
              strncat(temp, suggestions[suggestion_index], LSH_RL_BUFSIZE - strlen(temp) - 1);
            }
            
            strncpy(buffer, temp, bufsize - 1);
          } else {
            // We're completing a command
            strncpy(buffer, suggestions[suggestion_index], bufsize - 1);
          }
          buffer[bufsize - 1] = '\0';
          position = strlen(buffer);
          
          // Redraw the line with accepted suggestion
          printf("\r\033[K%s%s", prompt_buffer, buffer);
          fflush(stdout);
          
          // Check if the accepted suggestion is a directory
          if (strlen(buffer) > 0 && buffer[strlen(buffer) - 1] == '/') {
            // It's a directory, update suggestions to show its contents
            update_suggestions(buffer, position);
            display_inline_suggestion(prompt_buffer, buffer, position);
          } else {
            // Also update suggestions for other completions
            update_suggestions(buffer, position);
            display_inline_suggestion(prompt_buffer, buffer, position);
          }
        } 
        // Otherwise enter menu mode if we have multiple suggestions
        else if (has_suggestion && suggestion_count > 1) {
          menu_mode = 1;
          suggestion_index = 0;  // Start with first suggestion
          refresh_display(prompt_buffer, buffer, position);
        }
      }
    } else if (c == KEY_UP && menu_mode) {
      // In menu mode, up arrow goes to previous suggestion
      if (suggestion_count > 0) {
        suggestion_index = (suggestion_index + suggestion_count - 1) % suggestion_count;
        
        // Update full_suggestion with the newly selected suggestion
        if (prefix_start > 0) {
          // We're completing an argument
          char path_part[LSH_RL_BUFSIZE] = "";
            
          // Extract the existing path part
          strncpy(path_part, &buffer[prefix_start], position - prefix_start);
          path_part[position - prefix_start] = '\0';
          
          // Find last slash in the existing path_part
          char *last_slash = strrchr(path_part, '/');
          
          if (last_slash) {
            // Get the directory part up to the last slash (including the slash)
            int dir_part_len = (last_slash - path_part) + 1;
            char dir_part[LSH_RL_BUFSIZE] = "";
            strncpy(dir_part, path_part, dir_part_len);
            dir_part[dir_part_len] = '\0';
            
            // Construct the complete path by keeping the command and directory part
            strncpy(full_suggestion, buffer, prefix_start);
            full_suggestion[prefix_start] = '\0';
            strncat(full_suggestion, dir_part, sizeof(full_suggestion) - strlen(full_suggestion) - 1);
            strncat(full_suggestion, suggestions[suggestion_index], sizeof(full_suggestion) - strlen(full_suggestion) - 1);
          } else {
            // No slash in current argument, normal behavior
            strncpy(full_suggestion, buffer, prefix_start);
            full_suggestion[prefix_start] = '\0';
            strncat(full_suggestion, suggestions[suggestion_index], sizeof(full_suggestion) - strlen(full_suggestion) - 1);
          }
        } else {
          // We're completing a command
          strncpy(full_suggestion, suggestions[suggestion_index], sizeof(full_suggestion) - 1);
        }
        full_suggestion[sizeof(full_suggestion) - 1] = '\0';
        
        refresh_display(prompt_buffer, buffer, position);
      }
    } else if (c == KEY_DOWN && menu_mode) {
      // In menu mode, down arrow goes to next suggestion
      if (suggestion_count > 0) {
        suggestion_index = (suggestion_index + 1) % suggestion_count;
        
        // Update full_suggestion with the newly selected suggestion
        if (prefix_start > 0) {
          // We're completing an argument
          char path_part[LSH_RL_BUFSIZE] = "";
            
          // Extract the existing path part
          strncpy(path_part, &buffer[prefix_start], position - prefix_start);
          path_part[position - prefix_start] = '\0';
          
          // Find last slash in the existing path_part
          char *last_slash = strrchr(path_part, '/');
          
          if (last_slash) {
            // Get the directory part up to the last slash (including the slash)
            int dir_part_len = (last_slash - path_part) + 1;
            char dir_part[LSH_RL_BUFSIZE] = "";
            strncpy(dir_part, path_part, dir_part_len);
            dir_part[dir_part_len] = '\0';
            
            // Construct the complete path by keeping the command and directory part
            strncpy(full_suggestion, buffer, prefix_start);
            full_suggestion[prefix_start] = '\0';
            strncat(full_suggestion, dir_part, sizeof(full_suggestion) - strlen(full_suggestion) - 1);
            strncat(full_suggestion, suggestions[suggestion_index], sizeof(full_suggestion) - strlen(full_suggestion) - 1);
          } else {
            // No slash in current argument, normal behavior
            strncpy(full_suggestion, buffer, prefix_start);
            full_suggestion[prefix_start] = '\0';
            strncat(full_suggestion, suggestions[suggestion_index], sizeof(full_suggestion) - strlen(full_suggestion) - 1);
          }
        } else {
          // We're completing a command
          strncpy(full_suggestion, suggestions[suggestion_index], sizeof(full_suggestion) - 1);
        }
        full_suggestion[sizeof(full_suggestion) - 1] = '\0';
        
        refresh_display(prompt_buffer, buffer, position);
      }
    } else if (c == KEY_UP && !menu_mode) {
      // Navigate history upward when not in menu mode
      char *history_entry = get_previous_history_entry(&history_position);
      if (history_entry) {
        // Clear current line
        printf("\r\033[K");
        
        // Copy history entry to buffer
        strncpy(buffer, history_entry, bufsize - 1);
        buffer[bufsize - 1] = '\0';
        position = strlen(buffer);
        
        // Display the history entry and update suggestions
        printf("%s%s", prompt_buffer, buffer);
        fflush(stdout);
        
        // Update suggestions after loading history
        update_suggestions(buffer, position);
        display_inline_suggestion(prompt_buffer, buffer, position);
      }
    } else if (c == KEY_DOWN && !menu_mode) {
      // Navigate history downward when not in menu mode
      char *history_entry = get_next_history_entry(&history_position);
      if (history_entry) {
        // Clear current line
        printf("\r\033[K");
        
        // Copy history entry to buffer
        strncpy(buffer, history_entry, bufsize - 1);
        buffer[bufsize - 1] = '\0';
        position = strlen(buffer);
        
        // Display the history entry and update suggestions
        printf("%s%s", prompt_buffer, buffer);
        fflush(stdout);
        
        // Update suggestions after loading history
        update_suggestions(buffer, position);
        display_inline_suggestion(prompt_buffer, buffer, position);
      } else {
        // At the end of history, clear the line
        printf("\r\033[K%s", prompt_buffer);
        buffer[0] = '\0';
        position = 0;
        
        // Clear suggestions since we have an empty line
        if (suggestions) {
          for (int i = 0; i < suggestion_count; i++) {
            if (suggestions[i]) free(suggestions[i]);
          }
          free(suggestions);
          suggestions = NULL;
          suggestion_count = 0;
        }
        has_suggestion = 0;
      }
    } else if (isprint(c)) {
      // Regular character - add it to the buffer
      if (position >= bufsize - 1) {
        bufsize += LSH_RL_BUFSIZE;
        char *new_buffer = realloc(buffer, bufsize);
        if (!new_buffer) {
          fprintf(stderr, "lsh: allocation error\n");
          free(buffer);
          exit(EXIT_FAILURE);
        }
        buffer = new_buffer;
      }
      
      buffer[position] = c;
      position++;
      buffer[position] = '\0';
      
      // Exit menu mode when typing
      if (menu_mode) {
        menu_mode = 0;
        clear_menu();
      }
      
      // Update with the new character and show suggestions
      update_suggestions(buffer, position);
      display_inline_suggestion(prompt_buffer, buffer, position);
    }
  }
  
  // Clean up suggestions
  if (suggestions) {
    for (int i = 0; i < suggestion_count; i++) {
      if (suggestions[i]) free(suggestions[i]);
    }
    free(suggestions);
    suggestions = NULL;
    suggestion_count = 0;
  }
  has_suggestion = 0;
  
  return buffer;
}

/**
 * Split a line into tokens
 *
 * @param line The line to split
 * @return NULL-terminated array of tokens
 */
char **lsh_split_line(char *line) {
  int bufsize = LSH_TOK_BUFSIZE;
  int position = 0;
  char **tokens = malloc(bufsize * sizeof(char *));
  char *token;
  char *rest = line;
  
  if (!tokens) {
    fprintf(stderr, "lsh: allocation error\n");
    exit(EXIT_FAILURE);
  }
  
  // Handle quotes and ensure we don't split inside quoted strings
  while ((token = parse_token(&rest)) != NULL) {
    tokens[position] = token;
    position++;
    
    if (position >= bufsize) {
      bufsize += LSH_TOK_BUFSIZE;
      tokens = realloc(tokens, bufsize * sizeof(char *));
      if (!tokens) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
      }
    }
  }
  
  tokens[position] = NULL;
  return tokens;
}

/**
 * Parse a token from a string, handling quoted strings
 *
 * @param str_ptr Pointer to the string to parse, will be updated
 * @return The parsed token, or NULL if no more tokens
 */
char *parse_token(char **str_ptr) {
  char *str = *str_ptr;
  char *token_start;
  
  // Skip leading whitespace
  while (*str && isspace(*str)) {
    str++;
  }
  
  if (*str == '\0') {
    *str_ptr = str;
    return NULL; // No more tokens
  }
  
  token_start = str;
  
  if (*str == '"' || *str == '\'') {
    // Handle quoted string
    char quote = *str;
    str++; // Skip the opening quote
    token_start = str; // Token starts after the quote
    
    // Find the closing quote
    while (*str && *str != quote) {
      str++;
    }
    
    if (*str == quote) {
      // Found closing quote
      *str = '\0'; // Terminate the token
      str++; // Move past the closing quote
    }
  } else {
    // Regular token (not quoted)
    while (*str && !isspace(*str)) {
      str++;
    }
    
    if (*str) {
      *str = '\0'; // Terminate the token
      str++; // Move past the terminator
    }
  }
  
  *str_ptr = str;
  return strdup(token_start);
}

/**
 * Split a line with pipes into separate command arrays
 *
 * @param line The line to split
 * @return NULL-terminated array of NULL-terminated token arrays
 */
char ***lsh_split_piped_line(char *line) {
  int cmd_bufsize = LSH_TOK_BUFSIZE;
  int cmd_position = 0;
  char ***commands = malloc(cmd_bufsize * sizeof(char **));
  char *cmd_str, *save_ptr;
  
  if (!commands) {
    fprintf(stderr, "lsh: allocation error\n");
    exit(EXIT_FAILURE);
  }
  
  // First split by pipe character
  cmd_str = strtok_r(line, "|", &save_ptr);
  
  while (cmd_str != NULL) {
    // Allocate space for this command
    commands[cmd_position] = lsh_split_line(cmd_str);
    cmd_position++;
    
    // Check if we need more space for commands
    if (cmd_position >= cmd_bufsize) {
      cmd_bufsize += LSH_TOK_BUFSIZE;
      commands = realloc(commands, cmd_bufsize * sizeof(char **));
      if (!commands) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
      }
    }
    
    cmd_str = strtok_r(NULL, "|", &save_ptr);
  }
  
  commands[cmd_position] = NULL;
  return commands;
}
