/**
 * shell.h
 * Core shell functionality
 */

#ifndef SHELL_H
#define SHELL_H

#include "common.h"
#include "structured_data.h"

/**
 * Display a welcome banner with BBQ sauce invention time
 */
void display_welcome_banner(void);

/**
 * Execute a command
 *
 * @param args Null-terminated array of command arguments
 * @return 1 to continue the shell, 0 to exit
 */
int lsh_execute(char **args);

/**
 * Execute a pipeline of commands
 *
 * @param commands Null-terminated array of command arrays
 * @return 1 to continue the shell, 0 to exit
 */
int lsh_execute_piped(char ***commands);

/**
 * Create a TableData structure from an ls command
 *
 * @param args Command arguments
 * @return TableData pointer (must be freed by caller) or NULL on error
 */
TableData* create_ls_table(char **args);

/**
 * Parse input line into an array of commands (for pipelines)
 *
 * @param line Input command line
 * @return Null-terminated array of command arrays
 */
char*** lsh_split_commands(char *line);

/**
 * Launch an external program
 *
 * @param args Null-terminated array of command arguments
 * @return Always returns 1 (continue the shell)
 */
int lsh_launch(char **args);

/**
 * Main shell loop
 */
void lsh_loop(void);

/**
 * Free memory for a command array from lsh_split_commands
 */
void free_commands(char ***commands);

/**
 * Initialize the status bar at the bottom of the screen
 *
 * @param fd File descriptor for terminal (STDOUT_FILENO)
 * @return 1 if successful, 0 on failure
 */
int init_status_bar(int fd);

/**
 * Temporarily hide the status bar before command execution
 *
 * @param fd File descriptor for terminal (STDOUT_FILENO)
 */
void hide_status_bar(int fd);

/**
 * Ensures there's space for the status bar by scrolling if needed
 *
 * @param fd File descriptor for terminal (STDOUT_FILENO)
 */
void ensure_status_bar_space(int fd);

/**
 * Check for terminal window resize and update status bar position
 *
 * @param fd File descriptor for terminal (STDOUT_FILENO)
 */
void check_console_resize(int fd);

/**
 * Update the status bar with Git information
 *
 * @param fd File descriptor for terminal (STDOUT_FILENO)
 * @param git_info The Git information to display in the status bar
 */
void update_status_bar(int fd, const char *git_info);

/**
 * Get the name of the parent and current directories from a path
 *
 * @param cwd The current working directory path
 * @param parent_dir_name Buffer to store the parent directory name
 * @param current_dir_name Buffer to store the current directory name
 * @param buf_size Size of the buffers
 */
void get_path_display(const char *cwd, char *parent_dir_name,
                      char *current_dir_name, size_t buf_size);

/**
 * Initialize the terminal for raw mode
 * 
 * @param orig_termios Original terminal settings to restore later
 * @return File descriptor for terminal
 */
int init_terminal(struct termios *orig_termios);

/**
 * Restore terminal to original settings
 * 
 * @param fd File descriptor for terminal
 * @param orig_termios Original terminal settings
 */
void restore_terminal(int fd, struct termios *orig_termios);

/**
 * Get console dimensions
 * 
 * @param fd File descriptor for terminal
 * @param width Pointer to store width
 * @param height Pointer to store height
 * @return 1 on success, 0 on failure
 */
int get_console_dimensions(int fd, int *width, int *height);

#endif // SHELL_H