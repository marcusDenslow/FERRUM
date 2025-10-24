
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
#include <dirent.h> // For DIR, opendir, readdir
#include <sys/stat.h> // For stat

// Global variables for status bar
static int g_console_width = 80;
static int g_status_line = 0;
static int g_normal_attributes = 7; // Default white on black
static int g_status_attributes = 12; // Red
static int g_status_bar_enabled = 0; // Flag to track if status bar is enabled
static struct termios g_orig_termios; // Original terminal settings

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

void restore_terminal(int fd, struct termios *orig_termios) {
    // Restore cursor and terminal settings
    printf("\033[?25h"); // Ensure cursor is visible
    printf("\033c");     // Reset terminal state
    
    if (tcsetattr(fd, TCSAFLUSH, orig_termios) == -1) {
        perror("tcsetattr");
    }
}

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

int init_status_bar(int fd) {
    int width, height;
    if (!get_console_dimensions(fd, &width, &height)) {
        return 0;
    }
    
    g_console_width = width;
    g_status_line = height;
    g_status_bar_enabled = 1;
    
    // Clear status line
    printf(ANSI_SAVE_CURSOR);
    printf("\033[%d;1H", height);
    printf("\033[2K"); // Clear entire line
    printf(ANSI_RESTORE_CURSOR);
    fflush(stdout);
    
    return 1;
}

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

TableData* create_ls_table(char **args) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char cwd[PATH_MAX];
    char file_path[PATH_MAX];
    
    // Create table headers
    char *headers[] = {"Name", "Size", "Type", "Modified"};
    TableData *table = create_table(headers, 4);
    if (!table) {
        fprintf(stderr, "lsh: failed to create table\n");
        return NULL;
    }
    
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("lsh: getcwd");
        free_table(table);
        return NULL;
    }
    
    dir = opendir(cwd);
    if (dir == NULL) {
        perror("lsh: opendir");
        free_table(table);
        return NULL;
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
            DataValue *row = (DataValue*)malloc(4 * sizeof(DataValue));
            if (!row) {
                fprintf(stderr, "lsh: allocation error\n");
                continue;
            }
            
            // Name column
            row[0].type = TYPE_STRING;
            row[0].value.str_val = strdup(entry->d_name);
            row[0].is_highlighted = S_ISDIR(file_stat.st_mode); // Highlight directories
            
            // Size column
            row[1].type = TYPE_SIZE;
            row[1].value.str_val = malloc(32);
            if (S_ISDIR(file_stat.st_mode)) {
                strcpy(row[1].value.str_val, "<DIR>");
            } else if (file_stat.st_size < 1024) {
                snprintf(row[1].value.str_val, 32, "%d B", (int)file_stat.st_size);
            } else if (file_stat.st_size < 1024 * 1024) {
                snprintf(row[1].value.str_val, 32, "%.1f KB", file_stat.st_size / 1024.0);
            } else {
                snprintf(row[1].value.str_val, 32, "%.1f MB", file_stat.st_size / (1024.0 * 1024.0));
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
                    } else if (strcasecmp(ext, "exe") == 0 || strcasecmp(ext, "bat") == 0 || 
                              strcasecmp(ext, "sh") == 0 || strcasecmp(ext, "com") == 0) {
                        row[2].value.str_val = strdup("Executable");
                    } else if (strcasecmp(ext, "txt") == 0 || strcasecmp(ext, "md") == 0 || 
                              strcasecmp(ext, "log") == 0) {
                        row[2].value.str_val = strdup("Text");
                    } else if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "png") == 0 || 
                              strcasecmp(ext, "gif") == 0 || strcasecmp(ext, "bmp") == 0) {
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
    return table;
}

char*** lsh_split_commands(char *line) {
    char **cmd_groups = NULL;
    char *cmd_group, *saveptr0;
    int group_count = 0, group_capacity = 10;
    char ***commands = NULL;
    char **command = NULL;
    char *cmd_str, *token, *saveptr1, *saveptr2;
    int cmd_count = 0, cmd_capacity = 10;
    int token_count = 0, token_capacity = 10;


    // Allocate initial command groups array
    cmd_groups = malloc(group_capacity * sizeof(char*));
    if (!cmd_groups) {
        perror("lsh: allocation error");
        return NULL;
    }

    // Split by && symbol
    cmd_group = strtok_r(line, "&&", &saveptr0);
    while (cmd_group != NULL) {
        // Trim whitespace
        while (*cmd_group && isspace(*cmd_group)) cmd_group++;
        cmd_groups[group_count] = strdup(cmd_group);
        group_count++;

        if (group_count >= group_capacity - 1) {
            group_capacity *= 2;
            cmd_groups = realloc(cmd_groups, group_capacity * sizeof(char*));
            if (!cmd_groups) {
                perror("lsh: allocation error");
                return NULL;
            }
        }
        cmd_group = strtok_r(NULL, "&&", &saveptr0);
    }
    cmd_groups[group_count] = NULL;

    char *current_cmd = cmd_groups[0];
    
    // Allocate initial commands array
    commands = malloc(cmd_capacity * sizeof(char**));
    if (!commands) {
        perror("lsh: allocation error");
        for (int i = 0; i < group_count; i++) {
            free(cmd_groups[i]);
        }
        free(cmd_groups);
        return NULL;
    }
    
    // Split by pipe symbol '|'
    cmd_str = strtok_r(current_cmd, "|", &saveptr1);
    while (cmd_str != NULL) {
        // Trim whitespace
        while (*cmd_str && isspace(*cmd_str)) cmd_str++;
        
        // Allocate command token array
        command = malloc(token_capacity * sizeof(char*));
        if (!command) {
            perror("lsh: allocation error");
            // Free previous allocations
            for (int i = 0; i < cmd_count; i++) {
                for (int j = 0; commands[i][j] != NULL; j++) {
                    free(commands[i][j]);
                }
                free(commands[i]);
            }
            free(commands);
            for (int i = 0; i < group_count; i++) {
                free(cmd_groups[i]);
            }
            free(cmd_groups);
            return NULL;
        }
        
        // Split command into tokens
        token_count = 0;
        token = strtok_r(cmd_str, " \t\r\n\a", &saveptr2);
        while (token != NULL) {
            // Check if we need to resize tokens array
            if (token_count >= token_capacity - 1) {
                token_capacity *= 2;
                command = realloc(command, token_capacity * sizeof(char*));
                if (!command) {
                    perror("lsh: allocation error");
                    // Free previous allocations
                    for (int i = 0; i < cmd_count; i++) {
                        for (int j = 0; commands[i][j] != NULL; j++) {
                            free(commands[i][j]);
                        }
                        free(commands[i]);
                    }
                    free(commands);
                    return NULL;
                }
            }
            
            command[token_count] = strdup(token);
            token_count++;
            token = strtok_r(NULL, " \t\r\n\a", &saveptr2);
        }
        command[token_count] = NULL; // Null-terminate the tokens array
        
        // Check if we need to resize commands array
        if (cmd_count >= cmd_capacity - 1) {
            cmd_capacity *= 2;
            commands = realloc(commands, cmd_capacity * sizeof(char**));
            if (!commands) {
                perror("lsh: allocation error");
                return NULL;
            }
        }
        
        commands[cmd_count] = command;
        cmd_count++;
        cmd_str = strtok_r(NULL, "|", &saveptr1);
    }
    
    commands[cmd_count] = NULL; // Null-terminate the commands array
    
    // Store the remaining command groups in the last position (if any)
    if (group_count > 1) {
        // Create a special command array for the command groups marker
        char **marker = malloc(sizeof(char*) * 2);
        if (!marker) {
            perror("lsh: allocation error");
            for (int i = 0; i < group_count; i++) {
                free(cmd_groups[i]);
            }
            free(cmd_groups);
            return NULL;
        }
        marker[0] = strdup("&&_COMMAND_GROUPS");
        marker[1] = NULL;
        
        // Create a special command array for the remaining groups
        char **cmd_group_array = malloc(sizeof(char*) * group_count);
        if (!cmd_group_array) {
            perror("lsh: allocation error");
            free(marker[0]);
            free(marker);
            for (int i = 0; i < group_count; i++) {
                free(cmd_groups[i]);
            }
            free(cmd_groups);
            return NULL;
        }
        
        // Copy all remaining command groups
        for (int i = 1; i < group_count; i++) {
            cmd_group_array[i-1] = strdup(cmd_groups[i]);
        }
        cmd_group_array[group_count-1] = NULL;
        
        // Allocate space for cmd_count + 2 commands
        // (original commands + cmd_group_array + marker)
        char ***new_commands = malloc((cmd_count + 3) * sizeof(char**));
        if (!new_commands) {
            perror("lsh: allocation error");
            for (int i = 0; cmd_group_array[i] != NULL; i++) {
                free(cmd_group_array[i]);
            }
            free(cmd_group_array);
            free(marker[0]);
            free(marker);
            for (int i = 0; i < group_count; i++) {
                free(cmd_groups[i]);
            }
            free(cmd_groups);
            return NULL;
        }
        
        // Copy original commands
        for (int i = 0; i < cmd_count; i++) {
            new_commands[i] = commands[i];
        }
        
        // Add the command groups and marker
        new_commands[cmd_count] = cmd_group_array;
        new_commands[cmd_count+1] = marker;
        new_commands[cmd_count+2] = NULL;
        
        // Free the old commands array but not its contents
        free(commands);
        commands = new_commands;
    }
    
    // Free the command groups array
    for (int i = 0; i < group_count; i++) {
        free(cmd_groups[i]);
    }
    free(cmd_groups);
    
    return commands;
}

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

int lsh_execute_piped(char ***commands) {
    // Count the number of commands in the pipeline
    int cmd_count = 0;
    while (commands[cmd_count] != NULL) {
        cmd_count++;
    }
    
    if (cmd_count == 0) {
        return 1; // Nothing to execute
    }
    
    // Check if this is a table operation starting with ls/dir
    if (strcmp(commands[0][0], "ls") == 0 || strcmp(commands[0][0], "dir") == 0) {
        // Create a table from ls command
        TableData *table = create_ls_table(commands[0]);
        if (!table) {
            return 1; // Error already printed
        }
        
        // Apply filters from the pipeline
        for (int i = 1; commands[i] != NULL; i++) {
            // Find the filter command
            char *filter_cmd = commands[i][0];
            int filter_idx = -1;
            
            for (int j = 0; j < filter_count; j++) {
                if (strcmp(filter_cmd, filter_str[j]) == 0) {
                    filter_idx = j;
                    break;
                }
            }
            
            if (filter_idx == -1) {
                fprintf(stderr, "lsh: unknown filter command: %s\n", filter_cmd);
                free_table(table);
                return 1;
            }
            
            // Apply the filter
            TableData *filtered_table = filter_func[filter_idx](table, &commands[i][1]);
            free_table(table); // Free the old table
            
            if (!filtered_table) {
                return 1; // Error already printed
            }
            
            table = filtered_table;
        }
        
        // Print the final table
        print_table(table);
        free_table(table);
        
        return 1;
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

void free_commands(char ***commands) {
    for (int i = 0; commands[i] != NULL; i++) {
        for (int j = 0; commands[i][j] != NULL; j++) {
            free(commands[i][j]);
        }
        free(commands[i]);
    }
    free(commands);
}

void display_welcome_banner(void) {
    printf(ANSI_COLOR_CYAN);
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║              Welcome to the LSH Shell (Linux)              ║\n");
    printf("║                                                            ║\n");
    printf("║  Type 'help' to see available commands                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf(ANSI_COLOR_RESET);
}

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
        
        // Check for pipes or && and parse into multiple commands if present
        if (strchr(line, '|') != NULL || strchr(line, '&') != NULL) {
            commands = lsh_split_commands(line);
            
            // Find if there are command groups (&&)
            int has_cmd_groups = 0;
            char **remaining_cmd_groups = NULL;
            char **marker = NULL;
            int cmd_count = 0;
            
            // Count commands and check for the special command groups marker
            while (commands[cmd_count] != NULL) {
                cmd_count++;
            }
            
            // We need at least 2 positions for command groups:
            // 1. The groups themselves
            // 2. The marker with "&&_COMMAND_GROUPS"
            if (cmd_count > 1) {
                // Get the last non-null command
                marker = commands[cmd_count-1];
                
                // Check if it's the marker
                if (marker != NULL && marker[0] != NULL && 
                    strcmp(marker[0], "&&_COMMAND_GROUPS") == 0) {
                    
                    has_cmd_groups = 1;
                    
                    // Get the command groups from the previous position
                    remaining_cmd_groups = commands[cmd_count-2];
                    
                    // Set these positions to NULL so they don't get processed
                    // as regular commands
                    commands[cmd_count-1] = NULL;
                    commands[cmd_count-2] = NULL;
                }
            }
            
            // Execute the first command or pipeline
            status = lsh_execute_piped(commands);
            
            // If successful and we have command groups, execute them sequentially
            if (status && has_cmd_groups && remaining_cmd_groups != NULL) {
                for (int i = 0; remaining_cmd_groups[i] != NULL; i++) {
                    char *cmd_group = remaining_cmd_groups[i];
                    
                    // Parse this command group
                    char *cmd_copy = strdup(cmd_group);
                    
                    // Check if it contains pipes
                    if (strchr(cmd_copy, '|') != NULL) {
                        char ***cmd_commands = lsh_split_commands(cmd_copy);
                        if (cmd_commands) {
                            status = lsh_execute_piped(cmd_commands);
                            free_commands(cmd_commands);
                        }
                    } else {
                        // Simple command without pipes
                        char **args = lsh_split_line(cmd_copy);
                        
                        // Check for corrections before executing
                        char **corrected_args = check_for_corrections(args);
                        if (corrected_args != NULL) {
                            for (int j = 0; args[j] != NULL; j++) {
                                free(args[j]);
                            }
                            free(args);
                            args = corrected_args;
                        }
                        
                        // Execute command
                        status = lsh_execute(args);
                        
                        // Free allocated memory
                        for (int j = 0; args[j] != NULL; j++) {
                            free(args[j]);
                        }
                        free(args);
                    }
                    
                    free(cmd_copy);
                    
                    // If a command failed, stop execution
                    if (!status) {
                        break;
                    }
                }
            }
            
            // Clean up marker and command groups if they exist
            if (has_cmd_groups && marker) {
                // Free marker
                if (marker[0]) free(marker[0]);
                free(marker);
                
                // Free command groups
                if (remaining_cmd_groups) {
                    for (int i = 0; remaining_cmd_groups[i] != NULL; i++) {
                        free(remaining_cmd_groups[i]);
                    }
                    free(remaining_cmd_groups);
                }
            }
            
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
