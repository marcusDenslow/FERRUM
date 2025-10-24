
#ifndef TAB_COMPLETE_H
#define TAB_COMPLETE_H

#include "common.h"

typedef struct {
  int is_after_pipe;         // Flag if cursor is right after a pipe
  int is_filter_command;     // Flag if current command is a filter command
  char cmd_before_pipe[64];  // Command before the pipe
  char filter_command[64];   // Current filter command
  char current_token[1024];  // Current token being typed
  int token_position;        // Position within current token
  int token_index;           // Index of current token in command
  int filter_arg_index;      // Argument index for filter command
  int has_current_field;     // Flag if current field is set
  int has_current_operator;  // Flag if current operator is set
  char current_field[64];    // Current field if known
  char current_operator[16]; // Current operator if known
} CommandContext;

typedef enum {
  ARG_TYPE_ANY,       // Any argument
  ARG_TYPE_FILE,      // Files only
  ARG_TYPE_DIRECTORY, // Directories only
  ARG_TYPE_BOOKMARK,  // Bookmark names
  ARG_TYPE_ALIAS,     // Alias names
  ARG_TYPE_BOTH,      // Both files and directories
  ARG_TYPE_FAVORITE_CITY,
  ARG_TYPE_THEME,
  ARG_TYPE_COMMAND,
} ArgumentType;

typedef struct {
  char *command;         // Command name
  ArgumentType arg_type; // Expected argument type
  char *description;     // Optional description
  int strict_match;      // If 1, only show suggestions of the expected type
                    // If 0, show suggestions of expected type but also allow
                    // other matches
} CommandArgInfo;

typedef struct {
  char **items;      // Array of suggestion strings
  int count;         // Number of suggestions
  int current_index; // Current index for cycling through suggestions
} SuggestionList;

// Tab completion functions
char *get_tab_completion(const char *buffer);
SuggestionList *get_suggestion_list(const char *buffer, const char *prefix);
void free_suggestion_list(SuggestionList *list);

// Initialize tab completion system
void init_tab_completion(void);

// Shutdown tab completion system
void shutdown_tab_completion(void);

#endif // TAB_COMPLETE_H
