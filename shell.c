/**
 * shell.c
 * Implementation of core shell functionality
 */

#include "shell.h"
#include "aliases.h" // Added for alias support
#include "autocorrect.h"
#include "bookmarks.h" // Added for bookmark support
#include "builtins.h"
#include "countdown_timer.h"
#include "favorite_cities.h"
#include "filters.h"
#include "git_integration.h" // Added for Git repository detection
#include "line_reader.h"
#include "persistent_history.h"
#include "structured_data.h"
#include "tab_complete.h" // Added for tab completion support
#include "themes.h"
#include <stdio.h>
#include <time.h> // Added for time functions
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>

// Global variables for status bar
static int g_console_width = 80;
static int g_status_line = 0;
static WORD g_normal_attributes = 7; // Default white on black
static WORD g_status_attributes = 12; // Red
static BOOL g_status_bar_enabled = FALSE; // Flag to track if status bar is enabled
static struct termios g_orig_termios; // Original terminal settings

/**
 * Initialize terminal for raw mode
 */
int init_terminal(struct termios *orig_termios) {
    int fd = STDIN_FILENO;
    
    if (!isatty(fd)) {
        fprintf(stderr, "Not running in a terminal\n");
        return -1;
    }
    
    // Get current terminal settings
    if (tcgetattr(fd, orig_termios) == -1) {
        perror("tcgetattr");
        return -1;
    }
    
    // Create raw terminal settings (make a copy first)
    struct termios raw = *orig_termios;
    
    // Disable canonical mode and echo, but preserve some control characters
    raw.c_iflag &= ~(ICRNL | IXON); // Disable CTRL-S and CTRL-Q and CR to NL translation
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // Disable echo, canonical mode, and signal keys
    raw.c_cc[VMIN] = 1;   // Wait for at least one character
    raw.c_cc[VTIME] = 0;  // No timeout
    
    // Apply modified terminal settings
    if (tcsetattr(fd, TCSAFLUSH, &raw) == -1) {
        perror("tcsetattr");
        return -1;
    }
    
    printf("\033[?25h"); // Ensure cursor is visible
    
    return fd;
}

/**
 * Restore terminal to original settings
 */
void restore_terminal(int fd, struct termios *orig_termios) {
    // Restore cursor and terminal settings
    printf("\033[?25h"); // Ensure cursor is visible
    printf("\033c");     // Reset terminal state
    
    if (tcsetattr(fd, TCSAFLUSH, orig_termios) == -1) {
        perror("tcsetattr");
    }
}

/**
 * Get console dimensions
 */
int get_console_dimensions(int fd, int *width, int *height) {
    struct winsize ws;
    
    if (ioctl(fd, TIOCGWINSZ, &ws) == -1) {
        perror("ioctl");
        return 0;
    }
    
    *width = ws.ws_col;
    *height = ws.ws_row;
    
    return 1;
}

/**
 * Temporarily hide the status bar before command execution
 */
void hide_status_bar(int fd) {
    // Skip if status bar is not enabled
    if (!g_status_bar_enabled)
        return;
    
    int width, height;
    if (!get_console_dimensions(fd, &width, &height)) {
        return;
    }
    
    // Save cursor position
    printf(ANSI_SAVE_CURSOR);
    
    // Move to status bar line and clear it
    printf("\033[%d;1H", height);
    printf("\033[2K"); // Clear entire line
    
    // Restore cursor position
    printf(ANSI_RESTORE_CURSOR);
    fflush(stdout);
}

/**
 * This function scrolls the console buffer up one line to make room for the
 * status bar when we're at the bottom of the screen.
 */
void ensure_status_bar_space(int fd) {
    int width, height;
    if (!get_console_dimensions(fd, &width, &height)) {
        return;
    }
    
    // First clear the status bar if it exists
    printf(ANSI_SAVE_CURSOR);
    
    // Move to status bar line and clear it
    printf("\033[%d;1H", height);
    printf("\033[2K"); // Clear entire line
    
    // Add a blank line above status bar for spacing
    printf("\033[%d;1H", height - 1);
    printf("\033[2K"); // Clear entire line
    
    // Restore cursor position
    printf(ANSI_RESTORE_CURSOR);
    fflush(stdout);
}

/**
 * Initialize the status bar at the bottom of the screen
 */
int init_status_bar(int fd) {
    int width, height;
    if (!get_console_dimensions(fd, &width, &height)) {
        return 0;
    }
    
    g_console_width = width;
    g_status_line = height;
    g_status_bar_enabled = TRUE;
    
    // Clear status line
    printf(ANSI_SAVE_CURSOR);
    printf("\033[%d;1H", height);
    printf("\033[2K"); // Clear entire line
    printf(ANSI_RESTORE_CURSOR);
    fflush(stdout);
    
    return 1;
}

/**
 * Check for console window resize and update status bar position
 */
void check_console_resize(int fd) {
    if (!g_status_bar_enabled)
        return;
    
    int width, height;
    if (!get_console_dimensions(fd, &width, &height)) {
        return;
    }
    
    // Check if dimensions have changed
    if (width != g_console_width || height != g_status_line) {
        g_console_width = width;
        g_status_line = height;
        
        // Redraw status bar at new position
        hide_status_bar(fd);
    }
}

/**
 * Update the status bar with Git information
 */
void update_status_bar(int fd, const char *git_info) {
    if (!g_status_bar_enabled)
        return;
    
    int width, height;
    if (!get_console_dimensions(fd, &width, &height)) {
        return;
    }
    
    g_console_width = width;
    g_status_line = height;
    
    // Get current time for status bar
    time_t rawtime;
    struct tm *timeinfo;
    char time_buffer[10];
    
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", timeinfo);
    
    // Get current directory for display
    char cwd[LSH_RL_BUFSIZE];
    char parent_dir[LSH_RL_BUFSIZE / 2];
    char current_dir[LSH_RL_BUFSIZE / 2];
    
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        get_path_display(cwd, parent_dir, current_dir, LSH_RL_BUFSIZE / 2);
    } else {
        strcpy(parent_dir, "unknown");
        strcpy(current_dir, "dir");
    }
    
    printf(ANSI_SAVE_CURSOR);
    
    // Move cursor to status line
    printf("\033[%d;1H", height);
    
    // Clear the status line
    printf("\033[2K");
    
    // Set status bar color (cyan background)
    printf(ANSI_BG_CYAN ANSI_COLOR_BLACK);
    
    // Format: [time] parent_dir/current_dir [git_info]
    printf(" %s ", time_buffer);
    printf(" %s/%s ", parent_dir, current_dir);
    
    if (git_info != NULL && strlen(git_info) > 0) {
        printf(" %s ", git_info);
    }
    
    // Fill the rest of line with space to color the entire bar
    printf("%*s", width - (int)strlen(time_buffer) - 
           (int)strlen(parent_dir) - (int)strlen(current_dir) - 
           (git_info ? (int)strlen(git_info) + 3 : 0) - 8, "");
           
    // Reset color
    printf(ANSI_COLOR_RESET);
    
    // Restore cursor position
    printf(ANSI_RESTORE_CURSOR);
    fflush(stdout);
}

/**
 * Get the name of the parent and current directories from a path
 */

void get_path_display(const char *cwd, char *parent_dir_name,
                     char *current_dir_name, size_t buf_size) {
    char path_copy[PATH_MAX];
    strncpy(path_copy, cwd, PATH_MAX - 1);
    path_copy[PATH_MAX - 1] = '\0';
    
    // Default values if we can't parse the path
    strncpy(parent_dir_name, ".", buf_size - 1);
    strncpy(current_dir_name, "unknown", buf_size - 1);
    parent_dir_name[buf_size - 1] = '\0';
    current_dir_name[buf_size - 1] = '\0';
    
    // Handle root directory special case
    if (strcmp(path_copy, "/") == 0) {
        strncpy(parent_dir_name, "/", buf_size - 1);
        strncpy(current_dir_name, "", buf_size - 1);
        return;
    }
    
    // Remove trailing slash if present
    size_t len = strlen(path_copy);
    if (len > 1 && path_copy[len - 1] == '/') {
        path_copy[len - 1] = '\0';
    }
    
    // Find the last component (current directory)
    char *last_slash = strrchr(path_copy, '/');
    if (!last_slash) {
        // No slash found, must be a relative path with no slashes
        strncpy(current_dir_name, path_copy, buf_size - 1);
        return;
    }
    
    // Extract current directory name
    strncpy(current_dir_name, last_slash + 1, buf_size - 1);
    
    // Handle paths directly under root
    if (last_slash == path_copy) {
        strncpy(parent_dir_name, "/", buf_size - 1);
        return;
    }
    
    // Temporarily terminate the string at the last slash
    *last_slash = '\0';
    
    // Find the previous slash to identify parent directory
    char *prev_slash = strrchr(path_copy, '/');
    
    if (prev_slash) {
        // Extract just the parent directory name
        strncpy(parent_dir_name, prev_slash + 1, buf_size - 1);
        
        // Special case: parent is root
        if (prev_slash == path_copy) {
            strncpy(parent_dir_name, "/", buf_size - 1);
        }
    } else {
        // No previous slash, parent is the remaining part
        strncpy(parent_dir_name, path_copy, buf_size - 1);
    }
}

/**
 * Launch an external program
 */
int lsh_launch(char **args) {
    pid_t pid, wpid;
    int status;
    
    // Fork a child process
    pid = fork();
    
    if (pid == 0) {
        // Child process
        if (execvp(args[0], args) == -1) {
            perror("lsh");
        }
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        // Error forking
        perror("lsh");
    } else {
        // Parent process
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
    
    return 1;
}

/**
 * Execute a command
 */
int lsh_execute(char **args) {
    if (args[0] == NULL) {
        // An empty command was entered
        return 1;
    }
    
    // Check if it's a built-in command
    for (int i = 0; i < lsh_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }
    
    // If not a built-in, check aliases and launch external
    char **alias_expansion = expand_alias(args);
    if (alias_expansion != NULL) {
        int result = lsh_execute(alias_expansion);
        // Free the memory allocated for alias expansion
        for (int i = 0; alias_expansion[i] != NULL; i++) {
            free(alias_expansion[i]);
        }
        free(alias_expansion);
        return result;
    }
    
    return lsh_launch(args);
}

/**
 * Execute a pipeline of commands
 */
int lsh_execute_piped(char ***commands) {
    // Count the number of commands in the pipeline
    int cmd_count = 0;
    while (commands[cmd_count] != NULL) {
        cmd_count++;
    }
    
    if (cmd_count == 0) {
        return 1; // Nothing to execute
    }
    
    if (cmd_count == 1) {
        // No piping needed
        return lsh_execute(commands[0]);
    }
    
    // Create pipes
    int pipes[cmd_count - 1][2];
    for (int i = 0; i < cmd_count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            return 1;
        }
    }
    
    // Create processes
    pid_t pids[cmd_count];
    for (int i = 0; i < cmd_count; i++) {
        pids[i] = fork();
        
        if (pids[i] == -1) {
            perror("fork");
            return 1;
        } else if (pids[i] == 0) {
            // Child process
            
            // Set up input from previous pipe
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }
            
            // Set up output to next pipe
            if (i < cmd_count - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }
            
            // Close all pipe fds
            for (int j = 0; j < cmd_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            // Execute the command
            if (execvp(commands[i][0], commands[i]) == -1) {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        }
    }
    
    // Parent process closes all pipe fds
    for (int i = 0; i < cmd_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all children
    for (int i = 0; i < cmd_count; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
    
    return 1;
}

/**
 * Free memory for a command array from lsh_split_commands
 */
void free_commands(char ***commands) {
    for (int i = 0; commands[i] != NULL; i++) {
        for (int j = 0; commands[i][j] != NULL; j++) {
            free(commands[i][j]);
        }
        free(commands[i]);
    }
    free(commands);
}

/**
 * Display a welcome banner
 */
void display_welcome_banner(void) {
    printf(ANSI_COLOR_CYAN);
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║              Welcome to the LSH Shell (Linux)              ║\n");
    printf("║                                                            ║\n");
    printf("║  Type 'help' to see available commands                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(ANSI_COLOR_RESET);
}

/**
 * Main shell loop
 */
void lsh_loop(void) {
    char *line;
    char **args;
    char ***commands = NULL;
    int status = 1;
    char git_info[LSH_RL_BUFSIZE] = {0};
    int terminal_fd;
    
    // Initialize terminal for raw mode
    terminal_fd = init_terminal(&g_orig_termios);
    
    // Initialize the status bar
    //init_status_bar(STDOUT_FILENO);
    
    // Initialize subsystems
    init_aliases();
    init_bookmarks();
    init_tab_completion();
    init_persistent_history();
    init_favorite_cities();
    init_themes();
    init_autocorrect();
    init_git_integration();
    
    // Display the welcome banner
    display_welcome_banner();
    
    do {
        // Check for console resize
        check_console_resize(STDOUT_FILENO);
        
        // Get Git status for current directory
        // If git_status() returns null, git_info[0] will remain 0
        char *git_status_info = get_git_status();
        if (git_status_info != NULL) {
            strncpy(git_info, git_status_info, LSH_RL_BUFSIZE - 1);
            git_info[LSH_RL_BUFSIZE - 1] = '\0';
            free(git_status_info);
        } else {
            git_info[0] = '\0';
        }
        
        // Update status bar with Git information
        update_status_bar(STDOUT_FILENO, git_info);
        
        // Get input from the user
        line = lsh_read_line();
        
        // Empty line check
        if (line == NULL || line[0] == '\0') {
            if (line != NULL) {
                free(line);
            }
            continue;
        }
        
        // Add line to persistent history
        add_to_history(line);
        
        // Check for pipes and parse into multiple commands if present
        if (strchr(line, '|') != NULL) {
            commands = lsh_split_piped_line(line);
            status = lsh_execute_piped(commands);
            free_commands(commands);
        } else {
            // Normal command parsing
            args = lsh_split_line(line);
            
            // Check for corrections before executing
            char **corrected_args = check_for_corrections(args);
            if (corrected_args != NULL) {
                // Free the original args
                for (int i = 0; args[i] != NULL; i++) {
                    free(args[i]);
                }
                free(args);
                args = corrected_args;
            }
            
            // Execute command
            status = lsh_execute(args);
            
            // Free allocated memory
            for (int i = 0; args[i] != NULL; i++) {
                free(args[i]);
            }
            free(args);
        }
        
        free(line);
    } while (status);
    
    // Shutdown subsystems
    shutdown_aliases();
    shutdown_bookmarks();
    shutdown_tab_completion();
    shutdown_persistent_history();
    shutdown_favorite_cities();
    shutdown_themes();
    shutdown_autocorrect();
    
    // Restore terminal
    restore_terminal(terminal_fd, &g_orig_termios);
}
