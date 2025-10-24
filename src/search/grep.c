
#include "grep.h"
#include "builtins.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <strings.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/ioctl.h> // For getting terminal size with TIOCGWINSZ
#include <termios.h> // For terminal control
#include <stdlib.h> // For malloc, free

#define MAX_PATTERN_LENGTH 1024
#define MAX_LINE_LENGTH 4096
#define MAX_FILE_SIZE (50 * 1024 * 1024) // 50MB max file size

// Function to determine if a path is a directory
bool is_directory(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0)
        return false;
    return S_ISDIR(statbuf.st_mode);
}

// Function to determine if a file is text file
bool is_text_file(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) return false;
    
    // Read first 1024 bytes
    unsigned char buffer[1024];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);
    
    if (bytes_read == 0) return true; // Empty files are considered text
    
    // Check for binary content
    for (size_t i = 0; i < bytes_read; i++) {
        // If we find a null byte or too many non-printable characters, consider it binary
        if (buffer[i] == 0 || (buffer[i] < 32 && buffer[i] != '\n' && buffer[i] != '\r' && buffer[i] != '\t')) {
            return false;
        }
    }
    
    return true;
}

// Function to precompute the Boyer-Moore bad character table
void compute_bad_char_table(const char *pattern, int pattern_len, int bad_char[256]) {
    int i;
    
    // Initialize all occurrences as -1
    for (i = 0; i < 256; i++)
        bad_char[i] = -1;
    
    // Fill the actual value of last occurrence of a character
    for (i = 0; i < pattern_len; i++)
        bad_char[(unsigned char)pattern[i]] = i;
}

// Function to precompute the Boyer-Moore good suffix table
void compute_good_suffix_table(const char *pattern, int pattern_len, int suffix[MAX_PATTERN_LENGTH], bool prefix[MAX_PATTERN_LENGTH]) {
    int i, j;
    
    // Initialize suffix and prefix arrays
    for (i = 0; i < pattern_len; i++) {
        suffix[i] = -1;
        prefix[i] = false;
    }
    
    // Case 1: pattern[i+1...m-1] matches suffix of pattern[0...m-1]
    for (i = 0; i < pattern_len - 1; i++) {
        j = i;
        int k = 0;
        
        // While matching suffix
        while (j >= 0 && pattern[j] == pattern[pattern_len - 1 - k]) {
            j--;
            k++;
            suffix[k] = j + 1;
        }
        
        // If we found a suffix that starts at position 0
        if (j == -1) prefix[k] = true;
    }
}

// Function to calculate the shift for the Boyer-Moore algorithm
int get_shift(int bad_char[256], int suffix[MAX_PATTERN_LENGTH], bool prefix[MAX_PATTERN_LENGTH], 
              int pattern_len, int j, char c) {
    // Case 1: Bad character rule
    int bad_char_shift = j - bad_char[(unsigned char)c];
    
    // Case 2: Good suffix rule
    int good_suffix_shift = 0;
    
    if (j < pattern_len - 1) {
        int k = pattern_len - 1 - j;
        
        if (suffix[k] != -1) {
            // Case 2a: Matching suffix
            good_suffix_shift = j + 1 - suffix[k];
        } else {
            // Case 2b: Matching prefix of pattern
            for (int r = j + 2; r <= pattern_len - 1; r++) {
                if (prefix[pattern_len - r]) {
                    good_suffix_shift = r;
                    break;
                }
            }
            // If no matching prefix found
            if (good_suffix_shift == 0)
                good_suffix_shift = pattern_len;
        }
    }
    
    return (bad_char_shift > good_suffix_shift) ? bad_char_shift : good_suffix_shift;
}

// Boyer-Moore search in a string
bool boyer_moore_search(const char *text, int text_len, const char *pattern, int pattern_len,
                        int *match_pos, bool ignore_case) {
    if (pattern_len > MAX_PATTERN_LENGTH) {
        fprintf(stderr, "Pattern too long (max %d characters)\n", MAX_PATTERN_LENGTH);
        return false;
    }
    
    if (pattern_len == 0) return false;
    
    int bad_char[256];
    int suffix[MAX_PATTERN_LENGTH];
    bool prefix[MAX_PATTERN_LENGTH];
    
    // Precompute tables
    compute_bad_char_table(pattern, pattern_len, bad_char);
    compute_good_suffix_table(pattern, pattern_len, suffix, prefix);
    
    int s = 0; // Starting position for the current attempt
    
    while (s <= text_len - pattern_len) {
        int j = pattern_len - 1;
        
        // Compare pattern with text from right to left
        while (j >= 0) {
            char text_char = text[s + j];
            char pattern_char = pattern[j];
            
            if (ignore_case) {
                text_char = tolower(text_char);
                pattern_char = tolower(pattern_char);
            }
            
            if (text_char != pattern_char)
                break;
            j--;
        }
        
        // If the pattern was found
        if (j < 0) {
            *match_pos = s;
            return true;
        }
        
        // Shift pattern according to the Boyer-Moore rules
        s += get_shift(bad_char, suffix, prefix, pattern_len, j, 
                      ignore_case ? tolower(text[s + j]) : text[s + j]);
    }
    
    return false; // Pattern not found
}

// Function to process a single file
int process_file(const char *file_path, const char *pattern, bool show_line_numbers, 
                bool ignore_case, bool fuzzy_match) {
    FILE *file = fopen(file_path, "r");
    if (!file) {
        fprintf(stderr, "Error: Unable to open file %s\n", file_path);
        return 0;
    }
    
    // Check file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file);
    
    if (file_size > MAX_FILE_SIZE) {
        fprintf(stderr, "Error: File %s is too large (max 50MB)\n", file_path);
        fclose(file);
        return 0;
    }
    
    // For fuzzy matching, we use a simpler approach 
    // (in a real implementation, you'd want a proper fuzzy algorithm)
    
    char line[MAX_LINE_LENGTH];
    int line_number = 0;
    int matches_found = 0;
    int pattern_len = strlen(pattern);
    
    while (fgets(line, MAX_LINE_LENGTH, file)) {
        line_number++;
        int line_len = strlen(line);
        
        // Remove newline character at the end if present
        if (line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';
            line_len--;
        }
        
        int match_pos = 0;
        bool match_found = false;
        
        if (fuzzy_match) {
            // Simple fuzzy matching - allows for character skipping
            // For a real implementation, use Levenshtein distance or another algorithm
            char *line_ptr = line;
            char *pattern_ptr = (char *)pattern;
            int matched_chars = 0;
            
            while (*line_ptr && *pattern_ptr) {
                char line_char = ignore_case ? tolower(*line_ptr) : *line_ptr;
                char pattern_char = ignore_case ? tolower(*pattern_ptr) : *pattern_ptr;
                
                if (line_char == pattern_char) {
                    matched_chars++;
                    pattern_ptr++;
                }
                line_ptr++;
            }
            
            // If we've matched at least 70% of the pattern's characters
            match_found = (*pattern_ptr == '\0' || 
                           (matched_chars >= pattern_len * 0.7));
        } else {
            // Exact matching using Boyer-Moore
            match_found = boyer_moore_search(line, line_len, pattern, pattern_len, 
                                            &match_pos, ignore_case);
        }
        
        if (match_found) {
            matches_found++;
            
            // Print the file name only once if multiple matches
            if (matches_found == 1) {
                printf("%s%s%s:\n", ANSI_COLOR_CYAN, file_path, ANSI_COLOR_RESET);
            }
            
            // Print line number if requested
            if (show_line_numbers) {
                printf("  %s%d:%s ", ANSI_COLOR_GREEN, line_number, ANSI_COLOR_RESET);
            } else {
                printf("  ");
            }
            
            // Print the matching line, highlighting the match
            if (!fuzzy_match) {
                // Print the line with the match highlighted
                for (int i = 0; i < line_len; i++) {
                    if (i == match_pos) {
                        printf("%s", ANSI_COLOR_RED);
                        for (int j = 0; j < pattern_len; j++) {
                            putchar(line[i + j]);
                        }
                        printf("%s", ANSI_COLOR_RESET);
                        i += pattern_len - 1;
                    } else {
                        putchar(line[i]);
                    }
                }
            } else {
                // For fuzzy matches, just print the line
                printf("%s", line);
            }
            printf("\n");
        }
    }
    
    fclose(file);
    return matches_found;
}

// Function to recursively search directories
int search_directory(const char *dir_path, const char *pattern, bool show_line_numbers,
                   bool ignore_case, bool recursive, bool fuzzy_match) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "Error: Unable to open directory %s\n", dir_path);
        return 0;
    }
    
    struct dirent *entry;
    int total_matches = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "%s/%s", dir_path, entry->d_name);
        
        if (is_directory(path)) {
            // If recursive flag is set, search subdirectories
            if (recursive) {
                total_matches += search_directory(path, pattern, show_line_numbers, 
                                                ignore_case, recursive, fuzzy_match);
            }
        } else {
            // Only process text files
            if (is_text_file(path)) {
                total_matches += process_file(path, pattern, show_line_numbers, 
                                            ignore_case, fuzzy_match);
            }
        }
    }
    
    closedir(dir);
    return total_matches;
}

int lsh_actual_grep(char **args) {
    if (args[1] == NULL) {
        printf("Usage: grep [options] pattern [file/directory]\n");
        printf("Options:\n");
        printf("  -n, --line-numbers  Show line numbers\n");
        printf("  -i, --ignore-case   Ignore case distinctions\n");
        printf("  -r, --recursive     Search directories recursively\n");
        printf("  -f, --fuzzy         Use fuzzy matching instead of exact\n");
        return 1;
    }
    
    // Parse options
    bool show_line_numbers = false;
    bool ignore_case = false;
    bool recursive = false;
    bool fuzzy_match = false;
    
    int arg_idx = 1;
    const char *pattern = NULL;
    
    // Parse options
    while (args[arg_idx] != NULL && args[arg_idx][0] == '-') {
        if (strcmp(args[arg_idx], "-n") == 0 || strcmp(args[arg_idx], "--line-numbers") == 0) {
            show_line_numbers = true;
        } else if (strcmp(args[arg_idx], "-i") == 0 || strcmp(args[arg_idx], "--ignore-case") == 0) {
            ignore_case = true;
        } else if (strcmp(args[arg_idx], "-r") == 0 || strcmp(args[arg_idx], "--recursive") == 0) {
            recursive = true;
        } else if (strcmp(args[arg_idx], "-f") == 0 || strcmp(args[arg_idx], "--fuzzy") == 0) {
            fuzzy_match = true;
        } else if (strcmp(args[arg_idx], "--help") == 0) {
            printf("Usage: grep [options] pattern [file/directory]\n");
            printf("Options:\n");
            printf("  -n, --line-numbers  Show line numbers\n");
            printf("  -i, --ignore-case   Ignore case distinctions\n");
            printf("  -r, --recursive     Search directories recursively\n");
            printf("  -f, --fuzzy         Use fuzzy matching instead of exact\n");
            return 1;
        } else {
            // Unknown option - treat as pattern
            pattern = args[arg_idx];
            arg_idx++;
            break;
        }
        arg_idx++;
    }
    
    // The pattern should be the next argument after options
    if (pattern == NULL) {
        if (args[arg_idx] == NULL) {
            fprintf(stderr, "Error: No pattern specified\n");
            return 1;
        }
        pattern = args[arg_idx++];
    }
    
    int total_matches = 0;
    
    // If no files/directories specified, search current directory
    if (args[arg_idx] == NULL) {
        if (recursive) {
            total_matches = search_directory(".", pattern, show_line_numbers, 
                                           ignore_case, recursive, fuzzy_match);
        } else {
            // Just search files in current directory, not recursively
            DIR *dir = opendir(".");
            if (!dir) {
                fprintf(stderr, "Error: Unable to open current directory\n");
                return 1;
            }
            
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                // Skip directories and non-text files
                char path[PATH_MAX];
                snprintf(path, PATH_MAX, "./%s", entry->d_name);
                
                if (!is_directory(path) && is_text_file(path)) {
                    total_matches += process_file(path, pattern, show_line_numbers, 
                                               ignore_case, fuzzy_match);
                }
            }
            closedir(dir);
        }
    } else {
        // Process each specified file/directory
        while (args[arg_idx] != NULL) {
            const char *path = args[arg_idx++];
            
            if (is_directory(path)) {
                total_matches += search_directory(path, pattern, show_line_numbers, 
                                               ignore_case, recursive, fuzzy_match);
            } else {
                if (is_text_file(path)) {
                    total_matches += process_file(path, pattern, show_line_numbers, 
                                               ignore_case, fuzzy_match);
                }
            }
        }
    }
    
    // Print summary
    if (total_matches == 0) {
        printf("No matches found\n");
    } else {
        printf("\nFound %d match%s\n", total_matches, (total_matches == 1 ? "" : "es"));
    }
    
    return 1;
}

void run_interactive_grep_session(void) {
    // Save original terminal settings to restore later
    struct termios old_tio, new_tio;
    tcgetattr(STDIN_FILENO, &old_tio);
    
    // Copy the old settings and modify for raw input
    new_tio = old_tio;
    new_tio.c_lflag &= ~(ICANON | ECHO); // Disable canonical mode and echo
    new_tio.c_cc[VMIN] = 1;             // Read one character at a time
    new_tio.c_cc[VTIME] = 0;            // No timeout
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

    // Initialize the search query
    char search_query[256] = "";
    char last_query[256] = "";
    
    // Variables for UI state
    int running = 1;
    int selected_index = 0;
    int file_selected_index = 0;
    int match_selected_index = 0;
    int results_count = 0;
    
    // Results data structure:
    // We'll organize results by file for easier context display
    typedef struct {
        char *line;              // The matching line
        int line_number;         // Line number in the file
        char *context[11];       // 5 lines before, the match line, 5 lines after
        int context_line_numbers[11]; // Line numbers for context lines
    } MatchLine;
    
    typedef struct {
        char *filename;          // Filename
        MatchLine *matches;      // Array of matches in this file
        int match_count;         // Number of matches in this file
    } FileMatches;
    
    FileMatches *file_matches = NULL;  // Array of file matches
    int file_match_count = 0;          // Number of files with matches
    
    char **file_list = NULL;     // List of all files
    int file_count = 0;          // Total number of files
    int current_view = 0;        // 0 = files, 1 = matches
    
    // Get terminal size
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int term_width = w.ws_col;
    int term_height = w.ws_row;
    int split_point = term_width / 2;
    
    // Load all files in the current directory to start with
    DIR *dir = opendir(".");
    if (!dir) {
        fprintf(stderr, "Error: Unable to open current directory\n");
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
        return;
    }
    
    // Count files first
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and .. directories
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
            
        // Skip directories, only show files
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "./%s", entry->d_name);
        if (!is_directory(path) && is_text_file(path)) {
            file_count++;
        }
    }
    
    // Allocate memory for file list
    file_list = (char **)malloc(file_count * sizeof(char *));
    if (!file_list) {
        closedir(dir);
        tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
        return;
    }
    
    // Fill file list
    rewinddir(dir);
    int file_idx = 0;
    while ((entry = readdir(dir)) != NULL && file_idx < file_count) {
        // Skip . and .. directories
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
            
        // Skip directories, only show files
        char path[PATH_MAX];
        snprintf(path, PATH_MAX, "./%s", entry->d_name);
        if (!is_directory(path) && is_text_file(path)) {
            file_list[file_idx++] = strdup(entry->d_name);
        }
    }
    closedir(dir);
    
    // Draw initial UI once
    printf(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME);
    printf("--- Interactive Grep Search ---  [Type to search | Tab: switch pane | Enter: open file | Ctrl+C: exit]\n\n");
    printf("Search: ");
    
    // Main interaction loop
    while (running) {
        // Check terminal size and detect resize
        struct winsize new_w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &new_w);
        
        // If terminal size changed, redraw the screen completely
        if (new_w.ws_col != term_width || new_w.ws_row != term_height) {
            w = new_w;
            term_width = w.ws_col;
            term_height = w.ws_row;
            split_point = term_width / 2;
            
            // Redraw the whole screen on resize
            printf(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME);
            printf("--- Interactive Grep Search ---  [Type to search | Tab: switch pane | Enter: open file | Ctrl+C: exit]\n\n");
            printf("Search: %s", search_query);
        }
        
        // Update just the search query part and clear the rest of the line
        printf("\033[3;8H%s\033[K", search_query);
        
        // Position cursor at the header row
        printf("\033[5;1H");
        
        // Draw the split view header
        printf("Files");
        // Move cursor to split point
        printf("\033[%dG", split_point);
        printf("| Matches");
        
        // Move to the next line for the divider
        printf("\n");
        
        // Clear the divider line and redraw it
        printf("\033[K");
        for (int i = 0; i < split_point - 1; i++) {
            printf("-");
        }
        printf("+");
        for (int i = split_point + 1; i < term_width; i++) {
            printf("-");
        }
        
        // Check if the query has changed and we need to run search again
        if (strcmp(search_query, last_query) != 0) {
            // Free previous results if any
            if (file_matches) {
                for (int i = 0; i < file_match_count; i++) {
                    for (int j = 0; j < file_matches[i].match_count; j++) {
                        free(file_matches[i].matches[j].line);
                        
                        // Free the context lines
                        for (int k = 0; k < 11; k++) {
                            if (file_matches[i].matches[j].context[k]) {
                                free(file_matches[i].matches[j].context[k]);
                            }
                        }
                    }
                    
                    free(file_matches[i].filename);
                    free(file_matches[i].matches);
                }
                free(file_matches);
                file_matches = NULL;
                file_match_count = 0;
            }
            
            results_count = 0;
            
            if (strlen(search_query) > 0) {
                // First pass: count files with matches and allocate the structure
                int files_with_matches = 0;
                int *matches_per_file = (int *)calloc(file_count, sizeof(int));
                
                if (!matches_per_file) {
                    fprintf(stderr, "Memory allocation error\n");
                    continue;
                }
                
                // First, count matches per file
                for (int i = 0; i < file_count; i++) {
                    char filepath[PATH_MAX];
                    snprintf(filepath, PATH_MAX, "./%s", file_list[i]);
                    
                    // Open the file
                    FILE *file = fopen(filepath, "r");
                    if (!file) continue;
                    
                    // Read each line and search for the pattern
                    char line[MAX_LINE_LENGTH];
                    int line_number = 0;
                    
                    while (fgets(line, MAX_LINE_LENGTH, file)) {
                        line_number++;
                        
                        // Remove newline if present
                        int line_len = strlen(line);
                        if (line_len > 0 && line[line_len - 1] == '\n') {
                            line[line_len - 1] = '\0';
                            line_len--;
                        }
                        
                        // Check if this line contains the search query
                        // Using simple case-sensitive search here for simplicity
                        if (strstr(line, search_query) != NULL) {
                            if (matches_per_file[i] == 0) {
                                files_with_matches++;
                            }
                            matches_per_file[i]++;
                            results_count++;
                        }
                    }
                    
                    fclose(file);
                }
                
                // Allocate the file matches array
                if (files_with_matches > 0) {
                    file_matches = (FileMatches *)malloc(files_with_matches * sizeof(FileMatches));
                    if (!file_matches) {
                        fprintf(stderr, "Memory allocation error\n");
                        free(matches_per_file);
                        continue;
                    }
                    
                    // Initialize the file matches array
                    file_match_count = 0;
                    for (int i = 0; i < file_count; i++) {
                        if (matches_per_file[i] > 0) {
                            // This file has matches, initialize its entry
                            file_matches[file_match_count].filename = strdup(file_list[i]);
                            file_matches[file_match_count].match_count = matches_per_file[i];
                            file_matches[file_match_count].matches = 
                                (MatchLine *)malloc(matches_per_file[i] * sizeof(MatchLine));
                            
                            if (!file_matches[file_match_count].matches) {
                                fprintf(stderr, "Memory allocation error\n");
                                // Cleanup what we've allocated so far
                                free(file_matches[file_match_count].filename);
                                for (int j = 0; j < file_match_count; j++) {
                                    free(file_matches[j].filename);
                                    free(file_matches[j].matches);
                                }
                                free(file_matches);
                                file_matches = NULL;
                                file_match_count = 0;
                                free(matches_per_file);
                                continue;
                            }
                            
                            // Initialize all matches for this file
                            for (int j = 0; j < matches_per_file[i]; j++) {
                                file_matches[file_match_count].matches[j].line = NULL;
                                for (int k = 0; k < 11; k++) {
                                    file_matches[file_match_count].matches[j].context[k] = NULL;
                                    file_matches[file_match_count].matches[j].context_line_numbers[k] = 0;
                                }
                            }
                            
                            file_match_count++;
                        }
                    }
                    
                    // Second pass: fill in the matches with line content and context
                    for (int file_idx = 0, i = 0; i < file_count && file_idx < file_match_count; i++) {
                        if (matches_per_file[i] == 0) continue;
                        
                        char filepath[PATH_MAX];
                        snprintf(filepath, PATH_MAX, "./%s", file_list[i]);
                        
                        // Read the whole file into memory to easily extract context
                        // This is used to provide the surrounding lines
                        char *file_lines[100000] = {NULL}; // Support for up to 100K lines per file
                        int total_lines = 0;
                        
                        FILE *file = fopen(filepath, "r");
                        if (!file) {
                            file_idx++;
                            continue;
                        }
                        
                        // Read the file into memory
                        char line[MAX_LINE_LENGTH];
                        while (fgets(line, MAX_LINE_LENGTH, file) && total_lines < 100000) {
                            // Remove newline if present
                            int line_len = strlen(line);
                            if (line_len > 0 && line[line_len - 1] == '\n') {
                                line[line_len - 1] = '\0';
                                line_len--;
                            }
                            
                            file_lines[total_lines++] = strdup(line);
                        }
                        
                        // Now that we have all lines, look for matches and collect context
                        int match_idx = 0;
                        for (int line_idx = 0; line_idx < total_lines && match_idx < matches_per_file[i]; line_idx++) {
                            // Check if this line contains the search query
                            if (strstr(file_lines[line_idx], search_query) != NULL) {
                                // This is a match, save it with context
                                file_matches[file_idx].matches[match_idx].line = strdup(file_lines[line_idx]);
                                file_matches[file_idx].matches[match_idx].line_number = line_idx + 1;
                                
                                // Get context: 5 lines before and 5 lines after
                                for (int ctx = 0; ctx < 11; ctx++) {
                                    int target_line = line_idx - 5 + ctx;
                                    
                                    if (target_line >= 0 && target_line < total_lines) {
                                        file_matches[file_idx].matches[match_idx].context[ctx] = 
                                            strdup(file_lines[target_line]);
                                        file_matches[file_idx].matches[match_idx].context_line_numbers[ctx] = 
                                            target_line + 1;
                                    } else {
                                        file_matches[file_idx].matches[match_idx].context[ctx] = strdup("");
                                        file_matches[file_idx].matches[match_idx].context_line_numbers[ctx] = 0;
                                    }
                                }
                                
                                match_idx++;
                            }
                        }
                        
                        // Free the file lines array
                        for (int line_idx = 0; line_idx < total_lines; line_idx++) {
                            free(file_lines[line_idx]);
                        }
                        
                        fclose(file);
                        file_idx++;
                    }
                }
                
                free(matches_per_file);
            }
            
            // Update last query
            strcpy(last_query, search_query);
            
            // Reset selection
            file_selected_index = 0;
            match_selected_index = 0;
            selected_index = 0;
        }
        
        // Calculate how many items we can display in each pane
        int max_display = term_height - 6; // Account for header and other UI elements
        
        // Draw files in left pane
        int display_count = file_count < max_display ? file_count : max_display;
        for (int i = 0; i < display_count; i++) {
            // Check if this file has matches
            int match_idx = -1;
            for (int j = 0; j < file_match_count; j++) {
                if (strcmp(file_list[i], file_matches[j].filename) == 0) {
                    match_idx = j;
                    break;
                }
            }
            
            // Highlight if selected and in file view
            if (i == selected_index && current_view == 0) {
                printf("\033[7m"); // Invert colors
                file_selected_index = i; // Track selected file
            }
            
            // Display match count for the file
            int match_count = (match_idx >= 0) ? file_matches[match_idx].match_count : 0;
            
            // Truncate filename if too long
            int max_filename_length = split_point - 10; // Leave room for match count
            if (strlen(file_list[i]) > max_filename_length) {
                printf(" %.%ds… (%d)", max_filename_length - 1, file_list[i], match_count);
            } else {
                printf(" %s (%d)", file_list[i], match_count);
            }
            
            // Reset colors and clear to end of line section
            printf("\033[0m\033[K");
            
            // Move cursor to split point
            printf("\033[%dG", split_point);
            printf("| ");
            
            // End of line for file entry
            printf("\033[K\n");
        }
        
        // After listing all files, use the rest of the right pane to show context
        // for the currently selected file
        int context_start_line = display_count + 1;
        int selected_file_match_idx = -1;
        
        // Find the index of the selected file in the file_matches array
        for (int i = 0; i < file_match_count; i++) {
            if (file_selected_index < file_count && 
                strcmp(file_list[file_selected_index], file_matches[i].filename) == 0) {
                selected_file_match_idx = i;
                break;
            }
        }
        
        // Set cursor position for context display in the right pane
        printf("\033[6;%dH", split_point + 2);
        
        // Display context for the selected file if it has matches
        if (selected_file_match_idx >= 0 && file_matches[selected_file_match_idx].match_count > 0) {
            // Title for the context view
            printf("%s%s - %d matches%s", 
                   ANSI_COLOR_CYAN,
                   file_matches[selected_file_match_idx].filename,
                   file_matches[selected_file_match_idx].match_count,
                   ANSI_COLOR_RESET);
            printf("\033[K\n");
            
            // Choose which match to display context for (first one by default)
            int match_to_display = 0;
            if (match_selected_index < file_matches[selected_file_match_idx].match_count) {
                match_to_display = match_selected_index;
            }
            
            // Move cursor to the next line in the right pane
            printf("\033[%d;%dH", 7, split_point + 2);
            
            // Display line number and match info
            printf("  %sMatch %d of %d at line %d:%s\033[K\n", 
                   ANSI_COLOR_GREEN,
                   match_to_display + 1, 
                   file_matches[selected_file_match_idx].match_count,
                   file_matches[selected_file_match_idx].matches[match_to_display].line_number,
                   ANSI_COLOR_RESET);
            
            // Move cursor to the next line in the right pane
            printf("\033[%d;%dH", 8, split_point + 2);
            
            // Display context (5 lines before, the line itself, and 5 lines after)
            printf("  %s----- Context -----%s\033[K\n", ANSI_COLOR_YELLOW, ANSI_COLOR_RESET);
            
            // Display the 11 context lines (5 before, match, 5 after)
            for (int ctx = 0; ctx < 11; ctx++) {
                // Get the context line
                char* ctx_line = file_matches[selected_file_match_idx].matches[match_to_display].context[ctx];
                int ctx_line_number = file_matches[selected_file_match_idx].matches[match_to_display].context_line_numbers[ctx];
                
                // Move cursor to the next line in the right pane
                printf("\033[%d;%dH", 9 + ctx, split_point + 2);
                
                if (ctx_line && ctx_line_number > 0) {
                    // The highlighted line is in position 5 (0-based index)
                    if (ctx == 5) {
                        printf("  %s%3d:%s %s%s%s\033[K", 
                               ANSI_COLOR_GREEN, ctx_line_number, ANSI_COLOR_RESET,
                               ANSI_COLOR_RED, ctx_line, ANSI_COLOR_RESET);
                    } else {
                        printf("  %s%3d:%s %s\033[K", 
                               ANSI_COLOR_GREEN, ctx_line_number, ANSI_COLOR_RESET, ctx_line);
                    }
                } else {
                    // Just print an empty line for padding if we're at file boundaries
                    printf("      \033[K");
                }
                
                // Don't add a newline here, as we're using cursor positioning instead
            }
        } else {
            // No matches for this file
            if (strlen(search_query) > 0) {
                printf("%sNo matches in selected file%s\033[K", ANSI_COLOR_YELLOW, ANSI_COLOR_RESET);
            } else {
                printf("%sType to search in files%s\033[K", ANSI_COLOR_YELLOW, ANSI_COLOR_RESET);
            }
        }
        
        // Clear any remaining lines in the right pane
        for (int i = (selected_file_match_idx >= 0 ? 20 : 7); i < term_height - 1; i++) {
            printf("\033[%d;%dH\033[K", i, split_point + 2);
        }
        
        // Position cursor at the bottom line
        printf("\033[%d;1H", term_height - 1);
        
        // Show result count at the bottom
        printf("\033[K"); // Clear the line
        if (strlen(search_query) > 0) {
            printf("Found %d match%s in %d file%s", 
                   results_count, 
                   results_count == 1 ? "" : "es",
                   file_match_count,
                   file_match_count == 1 ? "" : "s");
        } else {
            printf("%d file%s available", 
                   file_count,
                   file_count == 1 ? "" : "s");
        }
        
        // Position cursor correctly at the end of the search query
        printf("\033[3;%dH", 8 + strlen(search_query));
        fflush(stdout);
        
        // Allow a small delay to reduce CPU usage and make the UI feel less flickery
        struct timespec ts = {0, 5000000}; // 5ms delay
        nanosleep(&ts, NULL);
        
        // Process user input
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            if (c == 3) { // Ctrl+C - exit
                running = 0;
            } else if (c == 9) { // Tab - switch between file and results view
                current_view = 1 - current_view;
                
                // Only switch to results if there are results
                if (current_view == 1 && results_count == 0) {
                    current_view = 0;
                }
            } else if (c == 10 || c == 13) { // Enter - open file
                if (current_view == 0 && selected_index < file_count) {
                    // Open the selected file in neovim
                    char filepath[PATH_MAX];
                    snprintf(filepath, PATH_MAX, "./%s", file_list[selected_index]);
                    
                    // If the file has matches, open at the line of the first match
                    int line_number = 1; // Default to line 1 if no matches
                    
                    // Find if this file has matches
                    int file_match_idx = -1;
                    for (int i = 0; i < file_match_count; i++) {
                        if (strcmp(file_list[selected_index], file_matches[i].filename) == 0) {
                            file_match_idx = i;
                            break;
                        }
                    }
                    
                    // If file has matches, use the line number of the currently selected match
                    if (file_match_idx >= 0 && file_matches[file_match_idx].match_count > 0) {
                        int match_idx = 0;
                        if (match_selected_index < file_matches[file_match_idx].match_count) {
                            match_idx = match_selected_index;
                        }
                        line_number = file_matches[file_match_idx].matches[match_idx].line_number;
                    }
                    
                    // Restore terminal settings before launching editor
                    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
                    
                    // Clear the screen
                    printf(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME);
                    
                    // Check if neovim is available
                    char command[2048];
                    if (system("which nvim >/dev/null 2>&1") == 0) {
                        snprintf(command, sizeof(command), "nvim +%d \"%s\"", line_number, filepath);
                    } else if (system("which vim >/dev/null 2>&1") == 0) {
                        snprintf(command, sizeof(command), "vim +%d \"%s\"", line_number, filepath);
                    } else {
                        snprintf(command, sizeof(command), "nano +%d \"%s\"", line_number, filepath);
                    }
                    
                    system(command);
                    
                    // Restore our terminal settings
                    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
                    
                    // Redraw UI
                    printf(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME);
                }
            } else if (c == 27) { // Escape sequence for arrow keys
                // Read the next two bytes
                char seq[2];
                if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                    if (seq[0] == '[') {
                        switch (seq[1]) {
                            case 'A': // Up arrow
                                if (selected_index > 0) selected_index--;
                                
                                // Reset match selection when changing files
                                match_selected_index = 0;
                                break;
                            case 'B': // Down arrow
                                if (current_view == 0 && selected_index < file_count - 1) {
                                    selected_index++;
                                    
                                    // Reset match selection when changing files
                                    match_selected_index = 0;
                                }
                                break;
                            case 'C': // Right arrow - next match in the current file
                                // Find the current file's match index
                                int file_match_idx = -1;
                                for (int i = 0; i < file_match_count; i++) {
                                    if (file_selected_index < file_count && 
                                        strcmp(file_list[file_selected_index], file_matches[i].filename) == 0) {
                                        file_match_idx = i;
                                        break;
                                    }
                                }
                                
                                // If file has matches, move to the next match
                                if (file_match_idx >= 0 && file_matches[file_match_idx].match_count > 0) {
                                    match_selected_index = (match_selected_index + 1) % 
                                                          file_matches[file_match_idx].match_count;
                                }
                                break;
                            case 'D': // Left arrow - previous match in the current file
                                // Find the current file's match index
                                file_match_idx = -1;
                                for (int i = 0; i < file_match_count; i++) {
                                    if (file_selected_index < file_count && 
                                        strcmp(file_list[file_selected_index], file_matches[i].filename) == 0) {
                                        file_match_idx = i;
                                        break;
                                    }
                                }
                                
                                // If file has matches, move to the previous match
                                if (file_match_idx >= 0 && file_matches[file_match_idx].match_count > 0) {
                                    match_selected_index = (match_selected_index - 1 + 
                                                          file_matches[file_match_idx].match_count) % 
                                                          file_matches[file_match_idx].match_count;
                                }
                                break;
                        }
                    }
                }
            } else if (c == 127 || c == 8) { // Backspace
                // Remove the last character from the search query
                size_t len = strlen(search_query);
                if (len > 0) {
                    search_query[len - 1] = '\0';
                }
            } else if (isprint(c)) { // Printable character
                // Add to the search query
                size_t len = strlen(search_query);
                if (len < sizeof(search_query) - 1) {
                    search_query[len] = c;
                    search_query[len + 1] = '\0';
                }
            }
        }
    }
    
    // Clean up
    if (file_matches) {
        for (int i = 0; i < file_match_count; i++) {
            for (int j = 0; j < file_matches[i].match_count; j++) {
                free(file_matches[i].matches[j].line);
                
                // Free the context lines
                for (int k = 0; k < 11; k++) {
                    if (file_matches[i].matches[j].context[k]) {
                        free(file_matches[i].matches[j].context[k]);
                    }
                }
            }
            
            free(file_matches[i].filename);
            free(file_matches[i].matches);
        }
        free(file_matches);
    }
    
    if (file_list) {
        for (int i = 0; i < file_count; i++) {
            free(file_list[i]);
        }
        free(file_list);
    }
    
    // Restore original terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    
    // Clear screen
    printf(ANSI_CLEAR_SCREEN ANSI_CURSOR_HOME);
}

int lsh_grep(char **args) {
    // If no arguments provided, run interactive grep
    if (args[1] == NULL) {
        run_interactive_grep_session();
        return 1;
    }
    
    // Otherwise run the regular grep command
    return lsh_actual_grep(args);
}