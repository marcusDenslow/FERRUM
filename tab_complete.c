/**
 * tab_complete.c
 * Linux implementation of tab completion
 */

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
    {"cd", ARG_TYPE_DIRECTORY, "Change current directory"},
    {"ls", ARG_TYPE_DIRECTORY, "List directory contents"},
    {"cat", ARG_TYPE_FILE, "Display file contents"},
    {"rm", ARG_TYPE_FILE, "Remove file"},
    {"mkdir", ARG_TYPE_DIRECTORY, "Make directory"},
    {"rmdir", ARG_TYPE_DIRECTORY, "Remove directory"},
    {"cp", ARG_TYPE_FILE, "Copy file or directory"},
    {"mv", ARG_TYPE_BOTH, "Move file or directory"},
    {"grep", ARG_TYPE_FILE, "Search file contents"},
    {"less", ARG_TYPE_FILE, "View file contents"},
    {"more", ARG_TYPE_FILE, "View file contents"},
    {"find", ARG_TYPE_DIRECTORY, "Find files"},
    {"chmod", ARG_TYPE_FILE, "Change file permissions"},
    {"chown", ARG_TYPE_FILE, "Change file owner"},
    {"tar", ARG_TYPE_FILE, "Archive utility"},
    {"gzip", ARG_TYPE_FILE, "Compress files"},
    {"gunzip", ARG_TYPE_FILE, "Decompress files"},
    {"zip", ARG_TYPE_FILE, "Compress files"},
    {"unzip", ARG_TYPE_FILE, "Decompress files"},
    {"bash", ARG_TYPE_FILE, "Run bash script"},
    {"sh", ARG_TYPE_FILE, "Run shell script"},
    {"python", ARG_TYPE_FILE, "Run Python script"},
    {"perl", ARG_TYPE_FILE, "Run Perl script"},
    {"java", ARG_TYPE_FILE, "Run Java program"},
    {"gcc", ARG_TYPE_FILE, "C compiler"},
    {"make", ARG_TYPE_FILE, "Build utility"},
    {"diff", ARG_TYPE_FILE, "Compare files"},
    {"patch", ARG_TYPE_FILE, "Apply patch file"},
    {"man", ARG_TYPE_ANY, "Display manual page"},
    {"help", ARG_TYPE_ANY, "Display help"},
    {"alias", ARG_TYPE_ALIAS, "Define or list aliases"},
    {"unalias", ARG_TYPE_ALIAS, "Remove alias"},
    {"bookmark", ARG_TYPE_BOOKMARK, "Bookmark directories"},
    {"weather", ARG_TYPE_FAVORITE_CITY, "Weather information"},
    {"theme", ARG_TYPE_THEME, "Shell theme settings"},
    {NULL, ARG_TYPE_ANY, NULL} // End marker
};

// Global state
static CommandContext current_context;

/**
 * Initialize tab completion system
 */
void init_tab_completion(void) {
  // Initialize context to defaults
  memset(&current_context, 0, sizeof(CommandContext));
}

/**
 * Shutdown tab completion system
 */
void shutdown_tab_completion(void) {
  // Nothing to clean up for now
}

/**
 * Get the type of argument expected for a command
 */
static ArgumentType get_argument_type(const char *cmd) {
  if (!cmd || !*cmd) return ARG_TYPE_ANY;
  
  for (int i = 0; command_arg_info[i].command != NULL; i++) {
    if (strcmp(command_arg_info[i].command, cmd) == 0) {
      return command_arg_info[i].arg_type;
    }
  }
  
  return ARG_TYPE_ANY;
}

/**
 * Parse the command line to determine context
 */
static void parse_command_context(const char *buffer) {
  // Reset the context
  memset(&current_context, 0, sizeof(CommandContext));
  
  if (!buffer || !*buffer) return;
  
  // Make a copy of the buffer to tokenize
  char buffer_copy[1024];
  strncpy(buffer_copy, buffer, sizeof(buffer_copy) - 1);
  buffer_copy[sizeof(buffer_copy) - 1] = '\0';
  
  // Tokenize the buffer
  char *token = strtok(buffer_copy, " \t");
  int token_index = 0;
  
  // Extract the command if present
  if (token) {
    strncpy(current_context.filter_command, token, sizeof(current_context.filter_command) - 1);
    current_context.filter_command[sizeof(current_context.filter_command) - 1] = '\0';
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
    strncpy(current_context.current_token, last_token, sizeof(current_context.current_token) - 1);
    current_context.current_token[sizeof(current_context.current_token) - 1] = '\0';
    current_context.token_index = token_index - 1;  // Adjust token index to be 0-based
  } else {
    // This handles the case where there's only one token (the command)
    // In this case, we're completing the command itself
    strncpy(current_context.current_token, current_context.filter_command, 
            sizeof(current_context.current_token) - 1);
    current_context.current_token[sizeof(current_context.current_token) - 1] = '\0';
    current_context.token_index = 0;
  }
}

/**
 * Find all files/directories that match the beginning of path
 */
static char *find_path_completions(const char *path) {
  if (!path || !*path) return NULL;
  
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
    // Skip "." and ".." if there's already a name prefix
    if (name_prefix_len > 0 && (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)) {
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
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
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
          snprintf(path_to_check, sizeof(path_to_check), "%s/%s", cwd, full_path);
        } else {
          strncpy(path_to_check, full_path, sizeof(path_to_check) - 1);
        }
      }
      
      path_to_check[sizeof(path_to_check) - 1] = '\0';
      
      if (stat(path_to_check, &st) == 0 && S_ISDIR(st.st_mode)) {
        char *dir_completion = (char *)malloc(strlen(full_path) + 2); // +2 for slash and null
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

/**
 * Get the completion for a command 
 */
static char *complete_command(const char *prefix) {
  if (!prefix || !*prefix) return NULL;
  
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
  if (!path) return NULL;
  
  char *path_copy = strdup(path);
  if (!path_copy) return NULL;
  
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

/**
 * Get completion for the current context
 */
char *get_tab_completion(const char *buffer) {
  if (!buffer) return NULL;
  
  // Parse the command context
  parse_command_context(buffer);
  
  // If we're completing the first token, it's a command
  if (current_context.token_index == 0) {
    return complete_command(current_context.current_token);
  }
  
  // For subsequent tokens, look at the command to determine what to complete
  ArgumentType arg_type = get_argument_type(current_context.filter_command);
  
  switch (arg_type) {
    case ARG_TYPE_FILE:
      // Complete file names
      return find_path_completions(current_context.current_token);
      
    case ARG_TYPE_DIRECTORY:
      // Complete directory names
      return find_path_completions(current_context.current_token);
      
    case ARG_TYPE_BOTH:
      // Complete both files and directories
      return find_path_completions(current_context.current_token);
      
    case ARG_TYPE_BOOKMARK:
      // Complete bookmark names
      if (current_context.current_token[0]) {
        int bookmark_count;
        char **bookmarks = get_bookmark_names(&bookmark_count);
        
        if (bookmarks) {
          char *result = NULL;
          for (int i = 0; i < bookmark_count; i++) {
            if (strncasecmp(bookmarks[i], current_context.current_token, strlen(current_context.current_token)) == 0) {
              result = strdup(bookmarks[i]);
              break;
            }
          }
          
          // Free the bookmark names
          for (int i = 0; i < bookmark_count; i++) {
            free(bookmarks[i]);
          }
          free(bookmarks);
          
          return result;
        }
      }
      break;
      
    case ARG_TYPE_ALIAS:
      // Complete alias names
      if (current_context.current_token[0]) {
        int alias_count;
        char **aliases = get_alias_names(&alias_count);
        
        if (aliases) {
          char *result = NULL;
          for (int i = 0; i < alias_count; i++) {
            if (strncasecmp(aliases[i], current_context.current_token, strlen(current_context.current_token)) == 0) {
              result = strdup(aliases[i]);
              break;
            }
          }
          
          // Free the alias names
          for (int i = 0; i < alias_count; i++) {
            free(aliases[i]);
          }
          free(aliases);
          
          return result;
        }
      }
      break;
      
    case ARG_TYPE_FAVORITE_CITY:
      // Complete favorite city names
      if (current_context.current_token[0]) {
        int city_count;
        char **cities = get_favorite_city_names(&city_count);
        
        if (cities) {
          char *result = NULL;
          for (int i = 0; i < city_count; i++) {
            if (strncasecmp(cities[i], current_context.current_token, strlen(current_context.current_token)) == 0) {
              result = strdup(cities[i]);
              break;
            }
          }
          
          // Free the city names
          for (int i = 0; i < city_count; i++) {
            free(cities[i]);
          }
          free(cities);
          
          return result;
        }
      }
      break;
      
    case ARG_TYPE_THEME:
      // Complete theme names
      if (current_context.current_token[0]) {
        int theme_count;
        char **themes = get_theme_names(&theme_count);
        
        if (themes) {
          char *result = NULL;
          for (int i = 0; i < theme_count; i++) {
            if (strncasecmp(themes[i], current_context.current_token, strlen(current_context.current_token)) == 0) {
              result = strdup(themes[i]);
              break;
            }
          }
          
          // Free the theme names
          for (int i = 0; i < theme_count; i++) {
            free(themes[i]);
          }
          free(themes);
          
          return result;
        }
      }
      break;
      
    case ARG_TYPE_ANY:
    default:
      // By default, complete file and directory names
      return find_path_completions(current_context.current_token);
  }
  
  return NULL;
}
