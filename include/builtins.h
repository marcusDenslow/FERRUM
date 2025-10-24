
#ifndef BUILTINS_H
#define BUILTINS_H

#include "common.h"
#include "system_monitor.h"
#include <time.h>

// Syntax highlighting definitions using ANSI colors
#define COLOR_DEFAULT ANSI_COLOR_RESET
#define COLOR_KEYWORD ANSI_COLOR_CYAN
#define COLOR_STRING ANSI_COLOR_GREEN
#define COLOR_COMMENT "\x1b[90m" // Dark gray
#define COLOR_NUMBER ANSI_COLOR_MAGENTA
#define COLOR_PREPROCESSOR ANSI_COLOR_YELLOW
#define COLOR_IDENTIFIER "\x1b[97m" // Bright white

// Add declarations for color functions
void set_color(int color);
void reset_color();

// History command implementation
#define HISTORY_SIZE 10

// New struct to hold history entries with timestamps
typedef struct {
  char *command;
  time_t timestamp;
} HistoryEntry;

// History variables - made extern to be accessible from line_reader.c
extern HistoryEntry command_history[HISTORY_SIZE];
extern int history_count;
extern int history_index;

// Built-in command declarations
int lsh_cd(char **args);
int lsh_help(char **args);
int lsh_exit(char **args);
int lsh_dir(char **args);
int lsh_clear(char **args);
int lsh_mkdir(char **args);
int lsh_rmdir(char **args);
int lsh_del(char **args);
int lsh_touch(char **args);
int lsh_pwd(char **args);
int lsh_cat(char **args);
int lsh_history(char **args);
int lsh_copy(char **args);
int lsh_paste(char **args);
int lsh_move(char **args);
int lsh_ps(char **args);
int lsh_news(char **args);
// Alias command declarations - added for alias support
int lsh_alias(char **args);
int lsh_unalias(char **args);
int lsh_aliases(char **args); // New command to list all aliases
// Bookmark command declarations - added for bookmark support
int lsh_bookmark(char **args);
int lsh_bookmarks(char **args);
int lsh_goto(char **args);
int lsh_unbookmark(char **args);
int lsh_focus_timer(char **args);
int lsh_weather(char **args);
// Text search command
int lsh_grep(char **args);
int lsh_actual_grep(char **args);
int lsh_ripgrep(char **args);
int lsh_fzf_native(char **args);
int lsh_clip(char **args);
int lsh_echo(char **args);
int lsh_self_destruct();
int lsh_theme(char **args);
int lsh_loc(char **args);
int lsh_git_status(char **args);
int lsh_gg(char **args);
int lsh_stats(char **args);

// Add command to history
void lsh_add_to_history(const char *command);

// Get number of builtin commands
int lsh_num_builtins(void);

// Expose the builtin command strings and function pointers
extern char *builtin_str[];
extern int (*builtin_func[])(char **);

char *extract_json_string(const char *json, const char *key);

#endif // BUILTINS_H
