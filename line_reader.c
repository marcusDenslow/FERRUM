/**
 * line_reader.c
 * Implementation of line reading and parsing with context-aware suggestions
 */

#include "line_reader.h"
#include "aliases.h"
#include "bookmarks.h" // Added for bookmark support
#include "builtins.h"  // Added for history access
#include "common.h"
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
  // We avoid modifying common.h by using local constants
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
      // Note: These may vary widely between terminals
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
  int show_suggestions = 0;
  
  // Suggestion state
  static int has_suggestion = 0;
  static int suggestion_index = 0;
  static char **suggestions = NULL;
  static int suggestion_count = 0;
  static char full_suggestion[LSH_RL_BUFSIZE] = {0};
  static int prefix_start = 0;
  
  // Define colors for suggestions
  #define SUGGESTION_COLOR "\033[2;37m"  // Dim white color for suggestions
  #define COUNTER_COLOR "\033[0;36m"     // Cyan color for the counter
  #define RESET_COLOR "\033[0m"
  
  if (!buffer) {
    fprintf(stderr, "lsh: allocation error\n");
    exit(EXIT_FAILURE);
  }
  
  // Clear the buffer
  buffer[0] = '\0';
  
  // Display prompt
  printf("\033[1;32m➜\033[0m ");
  fflush(stdout);

  // Function to check if current input exactly matches current suggestion
  int is_complete_match() {
    if (!has_suggestion || suggestion_count == 0) {
      return 0;  // No suggestion to compare with
    }
    
    // If we're typing a command (not an argument)
    if (prefix_start == 0) {
      // Check if buffer exactly matches the current suggestion
      return (strcasecmp(buffer, suggestions[suggestion_index]) == 0);
    } else {
      // We're completing an argument
      // Check if the part after the last space exactly matches the suggestion
      char *arg_part = buffer + prefix_start;
      return (strcasecmp(arg_part, suggestions[suggestion_index]) == 0);
    }
  }

  // Function to update and display suggestions
  void update_suggestions() {
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
            
            if (strcmp(dir_path, ".") == 0) {
              strncpy(full_path, entry->d_name, sizeof(full_path) - 1);
            } else if (dir_path[0] == '/' && dir_path[1] == '\0') {
              snprintf(full_path, sizeof(full_path) - 1, "/%s", entry->d_name);
            } else {
              snprintf(full_path, sizeof(full_path) - 1, "%s/%s", dir_path, entry->d_name);
            }
            
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
      has_suggestion = 1;
      suggestion_index = 0;
      
      // Create the full suggestion string that Enter would accept
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
          // Find where the current argument ends in the suggestion
          char *suggestion_part = suggestions[suggestion_index];
          if (strncasecmp(suggestion_part, current_arg, current_len) == 0) {
            strncpy(suggestion_text, suggestion_part + current_len, 
                    sizeof(suggestion_text) - 1);
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
        printf("\033[1;32m➜\033[0m %s", buffer);
        fflush(stdout);
      } else {
        // Clear the current line
        printf("\r\033[K");
        
        // Display prompt and current text
        printf("\033[1;32m➜\033[0m %s", current_text);
        
        // Display the suggestion part in dim color
        printf("%s%s%s", SUGGESTION_COLOR, suggestion_text, RESET_COLOR);
        
        // Show counter for which suggestion we're on
        if (suggestion_count > 1) {
          printf(" %s(%d/%d)%s", COUNTER_COLOR, suggestion_index + 1, suggestion_count, RESET_COLOR);
          
          // Move cursor back to end of actual input (including the counter)
          char counter_str[20];
          snprintf(counter_str, sizeof(counter_str), " (%d/%d)", suggestion_index + 1, suggestion_count);
          int counter_len = strlen(counter_str);
          int suggestion_len = strlen(suggestion_text);
          
          for (int i = 0; i < suggestion_len + counter_len; i++) {
            printf("\b");
          }
        } else {
          // Just move cursor back to end of actual input
          int suggestion_len = strlen(suggestion_text);
          for (int i = 0; i < suggestion_len; i++) {
            printf("\b");
          }
        }
        
        fflush(stdout);
      }
    } else {
      // No suggestions, just redraw the current line
      printf("\r\033[K\033[1;32m➜\033[0m %s", buffer);
      fflush(stdout);
    }
  }
  
  // Function to display the current suggestion (used after tab cycling)
  void display_current_suggestion() {
    if (has_suggestion && suggestion_count > 0) {
      // Create the full suggestion string
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
          // Find where the current argument ends in the suggestion
          char *suggestion_part = suggestions[suggestion_index];
          if (strncasecmp(suggestion_part, current_arg, current_len) == 0) {
            strncpy(suggestion_text, suggestion_part + current_len, 
                    sizeof(suggestion_text) - 1);
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
      
      // Clear the current line
      printf("\r\033[K");
      
      // Display prompt and current text
      printf("\033[1;32m➜\033[0m %s", current_text);
      
      // Display the suggestion part in dim color
      printf("%s%s%s", SUGGESTION_COLOR, suggestion_text, RESET_COLOR);
      
      // Show counter for which suggestion we're on
      if (suggestion_count > 1) {
        printf(" %s(%d/%d)%s", COUNTER_COLOR, suggestion_index + 1, suggestion_count, RESET_COLOR);
        
        // Move cursor back to end of actual input (including the counter)
        char counter_str[20];
        snprintf(counter_str, sizeof(counter_str), " (%d/%d)", suggestion_index + 1, suggestion_count);
        int counter_len = strlen(counter_str);
        int suggestion_len = strlen(suggestion_text);
        
        for (int i = 0; i < suggestion_len + counter_len; i++) {
          printf("\b");
        }
      } else {
        // Just move cursor back to end of actual input
        int suggestion_len = strlen(suggestion_text);
        for (int i = 0; i < suggestion_len; i++) {
          printf("\b");
        }
      }
      
      fflush(stdout);
    }
  }

  // Flag to track if we're in suggestion mode or execution mode
  int suggestion_active = 1;  // Start in suggestion mode 

  while (1) {
    c = read_key();
    
    if (c == KEY_ENTER || c == '\n' || c == '\r') {
      // Check if we have a visible suggestion (not an exact match)
      if (has_suggestion && suggestion_count > 0) {
        // Extract just the suggestion part to see if there's anything to complete
        char suggestion_text[LSH_RL_BUFSIZE] = "";
        
        if (prefix_start > 0) {
          char current_arg[LSH_RL_BUFSIZE] = "";
          strncpy(current_arg, &buffer[prefix_start], position - prefix_start);
          current_arg[position - prefix_start] = '\0';
          
          if (strncasecmp(suggestions[suggestion_index], current_arg, strlen(current_arg)) == 0) {
            strncpy(suggestion_text, 
                   suggestions[suggestion_index] + strlen(current_arg),
                   sizeof(suggestion_text) - 1);
          }
        } else {
          if (strncasecmp(suggestions[suggestion_index], buffer, strlen(buffer)) == 0) {
            strncpy(suggestion_text, 
                   suggestions[suggestion_index] + strlen(buffer),
                   sizeof(suggestion_text) - 1);
          }
        }
        
        // If there's a non-empty suggestion part and in suggestion mode, accept it
        if (strlen(suggestion_text) > 0 && suggestion_active) {
          // First Enter press with an active suggestion: Accept it without executing
          strncpy(buffer, full_suggestion, bufsize - 1);
          buffer[bufsize - 1] = '\0';
          position = strlen(buffer);
          
          // Redraw line with full accepted suggestion
          printf("\r\033[K\033[1;32m➜\033[0m %s", buffer);
          fflush(stdout);
          
          // Switch to execution mode for next Enter press
          suggestion_active = 0;
          
          // Reset suggestion state since we've accepted it
          has_suggestion = 0;
          if (suggestions) {
            for (int i = 0; i < suggestion_count; i++) {
              if (suggestions[i]) free(suggestions[i]);
            }
            free(suggestions);
            suggestions = NULL;
            suggestion_count = 0;
          }
        } else {
          // No visible suggestion or already accepted: Execute
          buffer[position] = '\0';
          printf("\n");
          fflush(stdout);
          break;
        }
      } else {
        // No suggestion: Execute the command
        buffer[position] = '\0';
        printf("\n");
        fflush(stdout);
        break;
      }
    } else if (c == KEY_BACKSPACE || c == 127) {
      // Handle backspace
      if (position > 0) {
        position--;
        buffer[position] = '\0';
        
        // Update suggestions after backspace
        suggestion_active = 1; // Re-enter suggestion mode
        update_suggestions();
      }
    } else if (c == KEY_TAB) {
      // If we have suggestions, cycle to the next one
      suggestion_active = 1; // Ensure we're in suggestion mode
      
      if (suggestion_count == 1) {
        // Only one suggestion - accept it but don't execute
        strncpy(buffer, full_suggestion, bufsize - 1);
        buffer[bufsize - 1] = '\0';
        position = strlen(buffer);
        
        // Redraw line with full accepted suggestion
        printf("\r\033[K\033[1;32m➜\033[0m %s", buffer);
        fflush(stdout);
        
        // Reset suggestion state to find new suggestions
        update_suggestions();
      } else if (suggestion_count > 1) {
        // Multiple suggestions - cycle to the next one
        suggestion_index = (suggestion_index + 1) % suggestion_count;
        display_current_suggestion();
      }
    } else if (c == KEY_UP) {
      // Navigate history upward
      char *history_entry = get_previous_history_entry(&history_position);
      if (history_entry) {
        // Clear current line
        printf("\r\033[K");
        
        // Copy history entry to buffer
        strncpy(buffer, history_entry, bufsize - 1);
        buffer[bufsize - 1] = '\0';
        position = strlen(buffer);
        
        // Display the history entry and update suggestions
        printf("\033[1;32m➜\033[0m %s", buffer);
        fflush(stdout);
        
        // Update suggestions after loading history
        suggestion_active = 1; // Re-enter suggestion mode
        update_suggestions();
      }
    } else if (c == KEY_DOWN) {
      // Navigate history downward
      char *history_entry = get_next_history_entry(&history_position);
      if (history_entry) {
        // Clear current line
        printf("\r\033[K");
        
        // Copy history entry to buffer
        strncpy(buffer, history_entry, bufsize - 1);
        buffer[bufsize - 1] = '\0';
        position = strlen(buffer);
        
        // Display the history entry and update suggestions
        printf("\033[1;32m➜\033[0m %s", buffer);
        fflush(stdout);
        
        // Update suggestions after loading history
        suggestion_active = 1; // Re-enter suggestion mode
        update_suggestions();
      } else {
        // At the end of history, clear the line
        printf("\r\033[K\033[1;32m➜\033[0m ");
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
        suggestion_active = 1; // Re-enter suggestion mode
      }
    } else if (c == '?') {
      // Toggle showing command suggestions
      show_suggestions = !show_suggestions;
      
      // Print the ? character
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
      printf("%c", c);
      
      if (show_suggestions) {
        // Show command suggestions
        printf("\nCommand suggestions:\n");
        
        // Check history for similar commands
        int suggestions_shown = 0;
        char **matching_commands = get_matching_history_entries(buffer);
        if (matching_commands) {
          for (int i = 0; matching_commands[i] != NULL && suggestions_shown < 5; i++) {
            printf("  %s\n", matching_commands[i]);
            suggestions_shown++;
          }
          free_matching_entries(matching_commands);
        }
        
        // Check built-in commands
        for (int i = 0; i < lsh_num_builtins() && suggestions_shown < 5; i++) {
          if (strncmp(buffer, builtin_str[i], position) == 0) {
            printf("  %s\n", builtin_str[i]);
            suggestions_shown++;
          }
        }
        
        // If we showed suggestions, redisplay the prompt and buffer
        if (suggestions_shown > 0) {
          printf("\033[1;32m➜\033[0m %s", buffer);
          fflush(stdout);
        }
        
        // Update suggestions after ? mode
        suggestion_active = 1; // Re-enter suggestion mode
        update_suggestions();
      }
    } else if (c == KEY_RIGHT && has_suggestion) {
      // Right arrow also accepts the current suggestion (without executing)
      strncpy(buffer, full_suggestion, bufsize - 1);
      buffer[bufsize - 1] = '\0';
      position = strlen(buffer);
      
      // Redraw line with full accepted suggestion
      printf("\r\033[K\033[1;32m➜\033[0m %s", buffer);
      fflush(stdout);
      
      // Reset suggestion state to find new suggestions
      suggestion_active = 1; // Re-enter suggestion mode
      update_suggestions();
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
      
      // Update with the new character and show suggestions
      suggestion_active = 1; // Re-enter suggestion mode when typing
      update_suggestions();
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
