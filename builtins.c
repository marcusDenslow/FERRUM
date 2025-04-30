/**
 * builtins.c
 * Implementation of all built-in shell commands
 */

#include "builtins.h"
#include "common.h"
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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>

// History variables
HistoryEntry command_history[HISTORY_SIZE];
int history_count = 0;
int history_index = 0;

// String array of built-in command names
char *builtin_str[] = {
    "cd",      "help",     "exit",      "dir",     "clear",
    "mkdir",   "rmdir",    "del",       "touch",   "pwd",
    "cat",     "history",  "copy",      "move",    "paste",
    "ps",      "news",     "alias",     "unalias", "aliases",
    "bookmark", "bookmarks", "goto",     "unbookmark", "focus_timer",
    "weather", "grep",     "grep-text", "ripgrep", "fzf",
    "clip",    "echo",     "theme",     "loc",     "git_status",
    "gg"};

// Array of function pointers to built-in command implementations
int (*builtin_func[])(char **) = {
    &lsh_cd,      &lsh_help,     &lsh_exit,      &lsh_dir,     &lsh_clear,
    &lsh_mkdir,   &lsh_rmdir,    &lsh_del,       &lsh_touch,   &lsh_pwd,
    &lsh_cat,     &lsh_history,  &lsh_copy,      &lsh_move,    &lsh_paste,
    &lsh_ps,      &lsh_news,     &lsh_alias,     &lsh_unalias, &lsh_aliases,
    &lsh_bookmark, &lsh_bookmarks, &lsh_goto,     &lsh_unbookmark, &lsh_focus_timer,
    &lsh_weather, &lsh_grep,     &lsh_actual_grep, &lsh_ripgrep, &lsh_fzf_native,
    &lsh_clip,    &lsh_echo,     &lsh_theme,     &lsh_loc,     &lsh_git_status,
    &lsh_gg};

/**
 * Set the console text color
 */
void set_color(int color) {
    switch(color) {
        case 0:  printf(ANSI_COLOR_RESET); break;
        case 1:  printf(ANSI_COLOR_RED); break;
        case 2:  printf(ANSI_COLOR_GREEN); break;
        case 3:  printf(ANSI_COLOR_YELLOW); break;
        case 4:  printf(ANSI_COLOR_BLUE); break;
        case 5:  printf(ANSI_COLOR_MAGENTA); break;
        case 6:  printf(ANSI_COLOR_CYAN); break;
        case 7:  printf(ANSI_COLOR_WHITE); break;
        default: printf(ANSI_COLOR_RESET);
    }
}

/**
 * Reset console color to default
 */
void reset_color() {
    printf(ANSI_COLOR_RESET);
}

/**
 * Get the number of built-in commands
 */
int lsh_num_builtins() { return sizeof(builtin_str) / sizeof(char *); }

/**
 * Add a command to the history
 */
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

/**
 * Built-in command: cd
 * Changes the current directory
 */
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

/**
 * Built-in command: help
 * Displays help information
 */
int lsh_help(char **args) {
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

    printf("\nFor more information on specific commands, type 'help <command>'\n");
    printf("Use tab completion for commands and file paths\n");
    printf("Use arrow keys to navigate command history\n");
    printf("Type a partial command followed by '?' for suggestions\n");

    return 1;
}

/**
 * Built-in command: exit
 * Exits the shell
 */
int lsh_exit(char **args) { return 0; }

/**
 * Built-in command: dir
 * Lists files in the current directory
 */
int lsh_dir(char **args) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char cwd[PATH_MAX];
    char file_path[PATH_MAX];
    int count = 0;
    int detailed = 0;

    // Check if we should show detailed listing
    if (args[1] != NULL && (strcmp(args[1], "-l") == 0 || strcmp(args[1], "--long") == 0)) {
        detailed = 1;
    }

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("lsh: getcwd");
        return 1;
    }

    dir = opendir(cwd);
    if (dir == NULL) {
        perror("lsh: opendir");
        return 1;
    }

    if (detailed) {
        printf("Mode       Size       Modified            Name\n");
        printf("----------------------------------------------------\n");
    }

    // First collect all entries
    typedef struct {
        char name[256];
        struct stat stat;
        int is_dir;
    } DirEntry;

    DirEntry *entries = malloc(1000 * sizeof(DirEntry)); // Arbitrary limit
    int entry_count = 0;

    while ((entry = readdir(dir)) != NULL && entry_count < 1000) {
        // Skip . and .. entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(file_path, sizeof(file_path), "%s/%s", cwd, entry->d_name);
        if (stat(file_path, &file_stat) == 0) {
            strcpy(entries[entry_count].name, entry->d_name);
            entries[entry_count].stat = file_stat;
            entries[entry_count].is_dir = S_ISDIR(file_stat.st_mode);
            entry_count++;
        }
    }

    closedir(dir);

    // Sort entries - directories first then files
    for (int i = 0; i < entry_count - 1; i++) {
        for (int j = i + 1; j < entry_count; j++) {
            if (entries[i].is_dir < entries[j].is_dir ||
                (entries[i].is_dir == entries[j].is_dir &&
                 strcmp(entries[i].name, entries[j].name) > 0)) {
                DirEntry temp = entries[i];
                entries[i] = entries[j];
                entries[j] = temp;
            }
        }
    }

    // Display entries
    for (int i = 0; i < entry_count; i++) {
        if (detailed) {
            // Format mode (simplified)
            char mode[11] = "----------";
            if (entries[i].is_dir) mode[0] = 'd';
            if (entries[i].stat.st_mode & S_IRUSR) mode[1] = 'r';
            if (entries[i].stat.st_mode & S_IWUSR) mode[2] = 'w';
            if (entries[i].stat.st_mode & S_IXUSR) mode[3] = 'x';
            if (entries[i].stat.st_mode & S_IRGRP) mode[4] = 'r';
            if (entries[i].stat.st_mode & S_IWGRP) mode[5] = 'w';
            if (entries[i].stat.st_mode & S_IXGRP) mode[6] = 'x';
            if (entries[i].stat.st_mode & S_IROTH) mode[7] = 'r';
            if (entries[i].stat.st_mode & S_IWOTH) mode[8] = 'w';
            if (entries[i].stat.st_mode & S_IXOTH) mode[9] = 'x';

            // Format size
            char size_str[20];
            if (entries[i].stat.st_size < 1024) {
                snprintf(size_str, sizeof(size_str), "%d B", (int)entries[i].stat.st_size);
            } else if (entries[i].stat.st_size < 1024 * 1024) {
                snprintf(size_str, sizeof(size_str), "%.1f KB",
                         entries[i].stat.st_size / 1024.0);
            } else {
                snprintf(size_str, sizeof(size_str), "%.1f MB",
                         entries[i].stat.st_size / (1024.0 * 1024.0));
            }

            // Format time
            char time_str[20];
            struct tm *tm_info = localtime(&entries[i].stat.st_mtime);
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);

            // Print entry with colors
            printf("%s %-10s %-17s ", mode, size_str, time_str);
            
            if (entries[i].is_dir) {
                printf(ANSI_COLOR_BLUE "%s" ANSI_COLOR_RESET "\n", entries[i].name);
            } else if (entries[i].stat.st_mode & S_IXUSR) {
                printf(ANSI_COLOR_GREEN "%s" ANSI_COLOR_RESET "\n", entries[i].name);
            } else {
                printf("%s\n", entries[i].name);
            }
        } else {
            count++;
            if (entries[i].is_dir) {
                printf(ANSI_COLOR_BLUE "%-20s" ANSI_COLOR_RESET, entries[i].name);
            } else if (entries[i].stat.st_mode & S_IXUSR) {
                printf(ANSI_COLOR_GREEN "%-20s" ANSI_COLOR_RESET, entries[i].name);
            } else {
                printf("%-20s", entries[i].name);
            }
            
            if (count % 4 == 0) {
                printf("\n");
            }
        }
    }

    free(entries);
    
    if (!detailed && count % 4 != 0) {
        printf("\n");
    }

    return 1;
}

/**
 * Built-in command: clear
 * Clears the screen
 */
int lsh_clear(char **args) {
    printf(ANSI_CLEAR_SCREEN);
    printf(ANSI_CURSOR_HOME);
    return 1;
}

/**
 * Built-in command: mkdir
 * Creates a new directory
 */
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

/**
 * Built-in command: rmdir
 * Removes a directory
 */
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

/**
 * Built-in command: del
 * Deletes a file
 */
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

/**
 * Built-in command: touch
 * Creates a new file or updates timestamp of existing file
 */
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

/**
 * Built-in command: pwd
 * Prints the current working directory
 */
int lsh_pwd(char **args) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("lsh: getcwd");
    }
    return 1;
}

/**
 * Built-in command: cat
 * Displays the contents of a file
 */
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

/**
 * Built-in command: history
 * Displays command history
 */
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

/**
 * Built-in command: copy
 * Copies a file
 */
int lsh_copy(char **args) {
    if (args[1] == NULL || args[2] == NULL) {
        fprintf(stderr, "lsh: expected source and destination arguments to \"copy\"\n");
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

/**
 * Built-in command: move
 * Moves a file
 */
int lsh_move(char **args) {
    if (args[1] == NULL || args[2] == NULL) {
        fprintf(stderr, "lsh: expected source and destination arguments to \"move\"\n");
        return 1;
    }

    if (rename(args[1], args[2]) != 0) {
        perror("lsh: move");
        return 1;
    }

    printf("Moved %s to %s\n", args[1], args[2]);
    return 1;
}

/**
 * Built-in command: paste
 * Placeholder for paste functionality
 */
int lsh_paste(char **args) {
    printf("Paste functionality not implemented yet\n");
    return 1;
}

/**
 * Built-in command: ps
 * Lists running processes
 */
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

/**
 * Built-in command: news
 * Placeholder for news functionality
 */
int lsh_news(char **args) {
    printf("Fetching news feed...\n");
    printf("News functionality not fully implemented yet\n");
    return 1;
}

/**
 * Built-in command: clip
 * Placeholder for clipboard functionality
 */
int lsh_clip(char **args) {
    printf("Clipboard functionality not implemented yet\n");
    return 1;
}

/**
 * Built-in command: echo
 * Echoes arguments to stdout
 */
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

/**
 * Built-in command: loc
 * Counts lines of code
 */
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
            while (*trimmed && isspace(*trimmed)) trimmed++;

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

/**
 * Helper function to extract a string value from a JSON response
 */
char *extract_json_string(const char *json, const char *key) {
    char search_key[100];
    sprintf(search_key, "\"%s\":", key);
    
    char *key_pos = strstr(json, search_key);
    if (!key_pos) return NULL;
    
    key_pos += strlen(search_key);
    
    // Skip whitespace
    while (*key_pos && isspace(*key_pos)) key_pos++;
    
    if (*key_pos != '"') return NULL;
    
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
    
    if (!*end) return NULL;
    
    // Allocate and copy the string value
    int len = end - key_pos;
    char *result = malloc(len + 1);
    if (!result) return NULL;
    
    strncpy(result, key_pos, len);
    result[len] = '\0';
    
    return result;
}

/**
 * Built-in command: git_status
 * Displays the Git status of the current repo
 */
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

/**
 * Built-in command: gg
 * Quick access to Git commands
 */
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
        printf("  b - branch\n");
        printf("  ch - checkout\n");
        printf("  o - open in GitHub browser\n");
        return 1;
    }

    // Execute the appropriate git command based on shorthand
    if (strcmp(args[1], "s") == 0) {
        system("git status");
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
            if (newline) *newline = '\0';
            
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
                        if (git_suffix) *git_suffix = '\0';
                        
                        sprintf(https_url, "https://github.com/%s", repo_path);
                    }
                } else if (strstr(remote_url, "https://github.com/")) {
                    // Already HTTPS format
                    strcpy(https_url, remote_url);
                    // Remove .git suffix if present
                    char *git_suffix = strstr(https_url, ".git");
                    if (git_suffix) *git_suffix = '\0';
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
                printf("No remote URL found. Is this a Git repository with a GitHub remote?\n");
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
        system("git push");
    } else if (strcmp(args[1], "a") == 0) {
        system("git add .");
    } else if (strcmp(args[1], "l") == 0) {
        system("git log --oneline -10");
    } else if (strcmp(args[1], "d") == 0) {
        system("git diff");
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
    } else {
        printf("Unknown git command shorthand: %s\n", args[1]);
    }

    return 1;
}