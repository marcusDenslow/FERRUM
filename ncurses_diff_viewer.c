/**
 * ncurses_diff_viewer.c
 * NCurses-based interactive diff viewer implementation
 */

#include "ncurses_diff_viewer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/**
 * Initialize the ncurses diff viewer
 */
int init_ncurses_diff_viewer(NCursesDiffViewer *viewer) {
    if (!viewer) return 0;
    
    memset(viewer, 0, sizeof(NCursesDiffViewer));
    viewer->selected_file = 0;
    viewer->file_scroll_offset = 0;
    viewer->current_mode = MODE_FILE_LIST;
    
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    
    // Enable colors
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_GREEN, COLOR_BLACK);   // Additions
        init_pair(2, COLOR_RED, COLOR_BLACK);     // Deletions
        init_pair(3, COLOR_CYAN, COLOR_BLACK);    // Headers
        init_pair(4, COLOR_YELLOW, COLOR_BLACK);  // Selected
        init_pair(5, COLOR_WHITE, COLOR_BLUE);    // Highlighted selection
    }
    
    getmaxyx(stdscr, viewer->terminal_height, viewer->terminal_width);
    viewer->file_panel_width = viewer->terminal_width * 0.3; // 30% of screen width
    viewer->file_panel_height = (viewer->terminal_height - 2) * 0.6; // 60% of available height
    viewer->commit_panel_height = (viewer->terminal_height - 2) - viewer->file_panel_height - 1; // Rest of height
    
    // Create three windows
    viewer->file_list_win = newwin(viewer->file_panel_height, viewer->file_panel_width, 1, 0);
    viewer->commit_list_win = newwin(viewer->commit_panel_height, viewer->file_panel_width, 
                                    1 + viewer->file_panel_height + 1, 0);
    viewer->file_content_win = newwin(viewer->terminal_height - 2, 
                                     viewer->terminal_width - viewer->file_panel_width - 1, 
                                     1, viewer->file_panel_width + 1);
    
    if (!viewer->file_list_win || !viewer->file_content_win || !viewer->commit_list_win) {
        cleanup_ncurses_diff_viewer(viewer);
        return 0;
    }
    
    return 1;
}

/**
 * Get list of changed files from git
 */
int get_ncurses_changed_files(NCursesDiffViewer *viewer) {
    if (!viewer) return 0;
    
    FILE *fp = popen("git status --porcelain 2>/dev/null", "r");
    if (!fp) return 0;
    
    viewer->file_count = 0;
    char line[512];
    
    while (fgets(line, sizeof(line), fp) != NULL && viewer->file_count < MAX_FILES) {
        // Remove newline
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        if (strlen(line) < 3) continue;
        
        // Parse git status format: "XY filename"
        char status = line[0];
        if (status == ' ') status = line[1]; // Check second column if first is space
        
        // Skip the status characters and space
        char *filename = line + 3;
        
        // Store file info
        viewer->files[viewer->file_count].status = status;
        viewer->files[viewer->file_count].marked_for_commit = 0; // Not marked by default
        strncpy(viewer->files[viewer->file_count].filename, filename, MAX_FILENAME_LEN - 1);
        viewer->files[viewer->file_count].filename[MAX_FILENAME_LEN - 1] = '\0';
        
        viewer->file_count++;
    }
    
    pclose(fp);
    return viewer->file_count;
}

/**
 * Create temporary file with current working version
 */
int create_temp_file_with_changes(const char *filename, char *temp_path) {
    snprintf(temp_path, 256, "/tmp/shell_diff_current_%d", getpid());
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\" 2>/dev/null", filename, temp_path);
    
    return (system(cmd) == 0);
}

/**
 * Create temporary file with git HEAD version
 */
int create_temp_file_git_version(const char *filename, char *temp_path) {
    snprintf(temp_path, 256, "/tmp/shell_diff_git_%d", getpid());
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git show HEAD:\"%s\" > \"%s\" 2>/dev/null", filename, temp_path);
    
    return (system(cmd) == 0);
}

/**
 * Check if a file is a new untracked file
 */
int is_ncurses_new_file(const char *filename) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git ls-files --error-unmatch \"%s\" 2>/dev/null", filename);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return 1; // Assume new if we can't check
    
    char output[256];
    int is_tracked = (fgets(output, sizeof(output), fp) != NULL);
    pclose(fp);
    
    return !is_tracked; // Return 1 if not tracked (new file)
}

/**
 * Load full file content with diff highlighting
 */
int load_full_file_with_diff(NCursesDiffViewer *viewer, const char *filename) {
    if (!viewer || !filename) return 0;
    
    viewer->file_line_count = 0;
    viewer->file_scroll_offset = 0;
    
    // Check if this is a new file
    if (is_ncurses_new_file(filename)) {
        // For new files, just show the content as all additions
        FILE *fp = fopen(filename, "r");
        if (!fp) return 0;
        
        char line[1024];
        while (fgets(line, sizeof(line), fp) != NULL && viewer->file_line_count < MAX_FULL_FILE_LINES) {
            // Remove newline
            char *newline_pos = strchr(line, '\n');
            if (newline_pos) *newline_pos = '\0';
            
            NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
            strncpy(file_line->line, line, sizeof(file_line->line) - 1);
            file_line->line[sizeof(file_line->line) - 1] = '\0';
            file_line->type = '+';
            file_line->is_diff_line = 1;
            
            viewer->file_line_count++;
        }
        
        fclose(fp);
        return viewer->file_line_count;
    }
    
    // For existing files, create temporary files and merge with diff
    char temp_current[256], temp_git[256];
    
    if (!create_temp_file_with_changes(filename, temp_current)) {
        return 0;
    }
    
    if (!create_temp_file_git_version(filename, temp_git)) {
        unlink(temp_current);
        return 0;
    }
    
    // Get diff information
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git diff HEAD -- \"%s\" 2>/dev/null", filename);
    
    FILE *diff_fp = popen(cmd, "r");
    if (!diff_fp) {
        unlink(temp_current);
        unlink(temp_git);
        return 0;
    }
    
    // Parse diff to understand which lines are changed
    char diff_line[1024];
    int current_old_line = 0, current_new_line = 0;
    int in_hunk = 0;
    
    // Store diff information for line marking
    int changed_lines[MAX_FULL_FILE_LINES];
    char line_types[MAX_FULL_FILE_LINES];
    memset(changed_lines, 0, sizeof(changed_lines));
    memset(line_types, ' ', sizeof(line_types));
    
    while (fgets(diff_line, sizeof(diff_line), diff_fp) != NULL) {
        if (diff_line[0] == '@' && diff_line[1] == '@') {
            // Hunk header: @@-old_start,old_count +new_start,new_count@@
            sscanf(diff_line, "@@-%d,%*d +%d,%*d@@", &current_old_line, &current_new_line);
            current_old_line--; // Will be incremented before use
            current_new_line--;
            in_hunk = 1;
        } else if (in_hunk) {
            if (diff_line[0] == '+') {
                current_new_line++;
                if (current_new_line < MAX_FULL_FILE_LINES) {
                    changed_lines[current_new_line] = 1;
                    line_types[current_new_line] = '+';
                }
            } else if (diff_line[0] == '-') {
                current_old_line++;
                // Mark the corresponding line in the new file as changed
                if (current_old_line < MAX_FULL_FILE_LINES) {
                    changed_lines[current_old_line] = 1;
                    line_types[current_old_line] = '-';
                }
            } else if (diff_line[0] == ' ') {
                current_old_line++;
                current_new_line++;
            }
        }
    }
    
    pclose(diff_fp);
    
    // Read the current file content and mark changed lines
    FILE *current_fp = fopen(temp_current, "r");
    if (!current_fp) {
        unlink(temp_current);
        unlink(temp_git);
        return 0;
    }
    
    char line[1024];
    int line_number = 1;
    
    while (fgets(line, sizeof(line), current_fp) != NULL && viewer->file_line_count < MAX_FULL_FILE_LINES) {
        // Remove newline
        char *newline_pos = strchr(line, '\n');
        if (newline_pos) *newline_pos = '\0';
        
        NCursesFileLine *file_line = &viewer->file_lines[viewer->file_line_count];
        strncpy(file_line->line, line, sizeof(file_line->line) - 1);
        file_line->line[sizeof(file_line->line) - 1] = '\0';
        
        // Check if this line is changed
        if (line_number < MAX_FULL_FILE_LINES && changed_lines[line_number]) {
            file_line->type = line_types[line_number];
            file_line->is_diff_line = 1;
        } else {
            file_line->type = ' ';
            file_line->is_diff_line = 0;
        }
        
        viewer->file_line_count++;
        line_number++;
    }
    
    fclose(current_fp);
    
    // Clean up temporary files
    unlink(temp_current);
    unlink(temp_git);
    
    return viewer->file_line_count;
}

/**
 * Draw a box with rounded corners
 */
void draw_rounded_box(WINDOW *win) {
    if (!win) return;
    
    int height, width;
    getmaxyx(win, height, width);
    
    // Draw horizontal lines
    for (int x = 1; x < width - 1; x++) {
        mvwaddch(win, 0, x, ACS_HLINE);
        mvwaddch(win, height - 1, x, ACS_HLINE);
    }
    
    // Draw vertical lines
    for (int y = 1; y < height - 1; y++) {
        mvwaddch(win, y, 0, ACS_VLINE);
        mvwaddch(win, y, width - 1, ACS_VLINE);
    }
    
    // Draw rounded corners
    mvwaddch(win, 0, 0, ACS_ULCORNER);
    mvwaddch(win, 0, width - 1, ACS_URCORNER);
    mvwaddch(win, height - 1, 0, ACS_LLCORNER);
    mvwaddch(win, height - 1, width - 1, ACS_LRCORNER);
}

/**
 * Get commit history
 */
int get_commit_history(NCursesDiffViewer *viewer) {
    if (!viewer) return 0;
    
    FILE *fp = popen("git log --oneline -20 --format=\"%h|%an|%s\" 2>/dev/null", "r");
    if (!fp) return 0;
    
    viewer->commit_count = 0;
    char line[512];
    
    // Get list of unpushed commits first
    char unpushed_hashes[MAX_COMMITS][16];
    int unpushed_count = 0;
    
    FILE *unpushed_fp = popen("git log origin/HEAD..HEAD --format=\"%h\" 2>/dev/null", "r");
    if (unpushed_fp) {
        while (fgets(line, sizeof(line), unpushed_fp) != NULL && unpushed_count < MAX_COMMITS) {
            char *newline = strchr(line, '\n');
            if (newline) *newline = '\0';
            strncpy(unpushed_hashes[unpushed_count], line, sizeof(unpushed_hashes[unpushed_count]) - 1);
            unpushed_hashes[unpushed_count][sizeof(unpushed_hashes[unpushed_count]) - 1] = '\0';
            unpushed_count++;
        }
        pclose(unpushed_fp);
    }
    
    // If origin/HEAD doesn't exist, try origin/main and origin/master
    if (unpushed_count == 0) {
        unpushed_fp = popen("git log origin/main..HEAD --format=\"%h\" 2>/dev/null", "r");
        if (unpushed_fp) {
            while (fgets(line, sizeof(line), unpushed_fp) != NULL && unpushed_count < MAX_COMMITS) {
                char *newline = strchr(line, '\n');
                if (newline) *newline = '\0';
                strncpy(unpushed_hashes[unpushed_count], line, sizeof(unpushed_hashes[unpushed_count]) - 1);
                unpushed_hashes[unpushed_count][sizeof(unpushed_hashes[unpushed_count]) - 1] = '\0';
                unpushed_count++;
            }
            pclose(unpushed_fp);
        }
    }
    
    if (unpushed_count == 0) {
        unpushed_fp = popen("git log origin/master..HEAD --format=\"%h\" 2>/dev/null", "r");
        if (unpushed_fp) {
            while (fgets(line, sizeof(line), unpushed_fp) != NULL && unpushed_count < MAX_COMMITS) {
                char *newline = strchr(line, '\n');
                if (newline) *newline = '\0';
                strncpy(unpushed_hashes[unpushed_count], line, sizeof(unpushed_hashes[unpushed_count]) - 1);
                unpushed_hashes[unpushed_count][sizeof(unpushed_hashes[unpushed_count]) - 1] = '\0';
                unpushed_count++;
            }
            pclose(unpushed_fp);
        }
    }
    
    while (fgets(line, sizeof(line), fp) != NULL && viewer->commit_count < MAX_COMMITS) {
        // Remove newline
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        // Parse format: hash|author|title
        char *hash = strtok(line, "|");
        char *author = strtok(NULL, "|");
        char *title = strtok(NULL, "|");
        
        if (hash && author && title) {
            // Store commit hash (first 7 chars)
            strncpy(viewer->commits[viewer->commit_count].hash, hash, sizeof(viewer->commits[viewer->commit_count].hash) - 1);
            viewer->commits[viewer->commit_count].hash[sizeof(viewer->commits[viewer->commit_count].hash) - 1] = '\0';
            
            // Store first two letters of author name
            viewer->commits[viewer->commit_count].author_initials[0] = author[0] ? author[0] : '?';
            viewer->commits[viewer->commit_count].author_initials[1] = author[1] ? author[1] : '?';
            viewer->commits[viewer->commit_count].author_initials[2] = '\0';
            
            // Store title
            strncpy(viewer->commits[viewer->commit_count].title, title, MAX_COMMIT_TITLE_LEN - 1);
            viewer->commits[viewer->commit_count].title[MAX_COMMIT_TITLE_LEN - 1] = '\0';
            
            // Check if this commit is in the unpushed list
            viewer->commits[viewer->commit_count].is_pushed = 1; // Default to pushed
            for (int i = 0; i < unpushed_count; i++) {
                if (strcmp(viewer->commits[viewer->commit_count].hash, unpushed_hashes[i]) == 0) {
                    viewer->commits[viewer->commit_count].is_pushed = 0;
                    break;
                }
            }
            
            viewer->commit_count++;
        }
    }
    
    pclose(fp);
    return viewer->commit_count;
}

/**
 * Toggle file marking for commit
 */
void toggle_file_mark(NCursesDiffViewer *viewer, int file_index) {
    if (!viewer || file_index < 0 || file_index >= viewer->file_count) return;
    
    viewer->files[file_index].marked_for_commit = !viewer->files[file_index].marked_for_commit;
}

/**
 * Mark all files for commit (or unmark if all are already marked)
 */
void mark_all_files(NCursesDiffViewer *viewer) {
    if (!viewer) return;
    
    // Check if all files are already marked
    int all_marked = 1;
    for (int i = 0; i < viewer->file_count; i++) {
        if (!viewer->files[i].marked_for_commit) {
            all_marked = 0;
            break;
        }
    }
    
    // If all are marked, unmark all. Otherwise, mark all
    for (int i = 0; i < viewer->file_count; i++) {
        viewer->files[i].marked_for_commit = all_marked ? 0 : 1;
    }
}

/**
 * Get commit title input from user
 */
int get_commit_title_input(char *title, int max_len) {
    if (!title) return 0;
    
    // Save current screen
    WINDOW *saved_screen = dupwin(stdscr);
    
    // Create a temporary window for input
    int start_y = LINES / 2 - 2;
    int start_x = COLS / 2 - 25;
    WINDOW *input_win = newwin(5, 50, start_y, start_x);
    
    if (!input_win) {
        if (saved_screen) delwin(saved_screen);
        return 0;
    }
    
    box(input_win, 0, 0);
    mvwprintw(input_win, 0, 2, " Commit Title ");
    mvwprintw(input_win, 2, 2, "Enter commit message:");
    mvwprintw(input_win, 4, 2, "Press Enter when done, Esc to cancel");
    wrefresh(input_win);
    
    // Enable echo and normal mode for input
    echo();
    nocbreak();
    cbreak();
    
    // Get input
    wmove(input_win, 3, 2);
    wgetnstr(input_win, title, max_len - 1);
    
    // Restore settings
    noecho();
    
    delwin(input_win);
    
    // Restore the screen
    if (saved_screen) {
        overwrite(saved_screen, stdscr);
        delwin(saved_screen);
    }
    
    // Force a complete redraw
    clear();
    refresh();
    
    return strlen(title) > 0 ? 1 : 0;
}

/**
 * Commit marked files with title
 */
int commit_marked_files(NCursesDiffViewer *viewer, const char *commit_title) {
    if (!viewer || !commit_title || strlen(commit_title) == 0) return 0;
    
    // First, add marked files to git
    for (int i = 0; i < viewer->file_count; i++) {
        if (viewer->files[i].marked_for_commit) {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "git add \"%s\" 2>/dev/null >/dev/null", viewer->files[i].filename);
            system(cmd);
        }
    }
    
    // Commit with the provided title
    char commit_cmd[1024];
    snprintf(commit_cmd, sizeof(commit_cmd), "git commit -m \"%s\" 2>/dev/null >/dev/null", commit_title);
    int result = system(commit_cmd);
    
    if (result == 0) {
        // Refresh file list and commit history
        get_ncurses_changed_files(viewer);
        get_commit_history(viewer);
        
        return 1;
    }
    
    return 0;
}

/**
 * Push specific commit
 */
int push_commit(NCursesDiffViewer *viewer, int commit_index) {
    if (!viewer || commit_index < 0 || commit_index >= viewer->commit_count) return 0;
    
    // Push to origin (hide output)
    int result = system("git push origin 2>/dev/null >/dev/null");
    
    if (result == 0) {
        // Refresh commit history to get proper push status
        get_commit_history(viewer);
        return 1;
    }
    
    return 0;
}

/**
 * Render the file list window
 */
void render_file_list_window(NCursesDiffViewer *viewer) {
    if (!viewer || !viewer->file_list_win) return;
    
    // Draw rounded border and title
    draw_rounded_box(viewer->file_list_win);
    mvwprintw(viewer->file_list_win, 0, 2, " 1. Files ");
    
    int max_files_visible = viewer->file_panel_height - 2;
    
    for (int i = 0; i < max_files_visible; i++) {
        int y = i + 1;
        
        // Clear this line first
        wmove(viewer->file_list_win, y, 1);
        wclrtoeol(viewer->file_list_win);
        
        // Skip if no more files
        if (i >= viewer->file_count) continue;
        
        // Show selection indicator
        if (i == viewer->selected_file) {
            if (viewer->current_mode == MODE_FILE_LIST) {
                wattron(viewer->file_list_win, COLOR_PAIR(5));
                mvwprintw(viewer->file_list_win, y, 1, ">");
            } else {
                wattron(viewer->file_list_win, COLOR_PAIR(1));
                mvwprintw(viewer->file_list_win, y, 1, "*");
            }
        } else {
            mvwprintw(viewer->file_list_win, y, 1, " ");
        }
        
        mvwprintw(viewer->file_list_win, y, 2, " ");
        
        // Status indicator
        char status = viewer->files[i].status;
        if (status == 'M') {
            wattron(viewer->file_list_win, COLOR_PAIR(4));
            mvwprintw(viewer->file_list_win, y, 3, "M");
            wattroff(viewer->file_list_win, COLOR_PAIR(4));
        } else if (status == 'A') {
            wattron(viewer->file_list_win, COLOR_PAIR(1));
            mvwprintw(viewer->file_list_win, y, 3, "A");
            wattroff(viewer->file_list_win, COLOR_PAIR(1));
        } else if (status == 'D') {
            wattron(viewer->file_list_win, COLOR_PAIR(2));
            mvwprintw(viewer->file_list_win, y, 3, "D");
            wattroff(viewer->file_list_win, COLOR_PAIR(2));
        } else {
            mvwprintw(viewer->file_list_win, y, 3, "%c", status);
        }
        
        // Filename (truncated to fit panel with better margins)
        int max_name_len = viewer->file_panel_width - 8; // More margin
        char truncated_name[256];
        if ((int)strlen(viewer->files[i].filename) > max_name_len) {
            strncpy(truncated_name, viewer->files[i].filename, max_name_len - 3);
            truncated_name[max_name_len - 3] = '\0';
            strcat(truncated_name, "...");
        } else {
            strcpy(truncated_name, viewer->files[i].filename);
        }
        
        // Color filename green if marked for commit
        if (viewer->files[i].marked_for_commit) {
            wattron(viewer->file_list_win, COLOR_PAIR(1));
        }
        mvwprintw(viewer->file_list_win, y, 5, "%s", truncated_name);
        if (viewer->files[i].marked_for_commit) {
            wattroff(viewer->file_list_win, COLOR_PAIR(1));
        }
        
        if (i == viewer->selected_file) {
            if (viewer->current_mode == MODE_FILE_LIST) {
                wattroff(viewer->file_list_win, COLOR_PAIR(5));
            } else {
                wattroff(viewer->file_list_win, COLOR_PAIR(1));
            }
        }
    }
    
    wrefresh(viewer->file_list_win);
}

/**
 * Render the commit list window
 */
void render_commit_list_window(NCursesDiffViewer *viewer) {
    if (!viewer || !viewer->commit_list_win) return;
    
    // Draw rounded border and title
    draw_rounded_box(viewer->commit_list_win);
    mvwprintw(viewer->commit_list_win, 0, 2, " 3. Commits ");
    
    int max_commits_visible = viewer->commit_panel_height - 2;
    
    for (int i = 0; i < max_commits_visible; i++) {
        int y = i + 1;
        
        // Clear this line first
        wmove(viewer->commit_list_win, y, 1);
        wclrtoeol(viewer->commit_list_win);
        
        // Skip if no more commits
        if (i >= viewer->commit_count) continue;
        
        // Show selection indicator for commit list mode
        if (i == viewer->selected_commit && viewer->current_mode == MODE_COMMIT_LIST) {
            wattron(viewer->commit_list_win, COLOR_PAIR(5));
            mvwprintw(viewer->commit_list_win, y, 1, ">");
            wattroff(viewer->commit_list_win, COLOR_PAIR(5));
        } else {
            mvwprintw(viewer->commit_list_win, y, 1, " ");
        }
        
        // Show commit hash with color based on push status
        if (viewer->commits[i].is_pushed) {
            wattron(viewer->commit_list_win, COLOR_PAIR(1)); // Green for pushed
        } else {
            wattron(viewer->commit_list_win, COLOR_PAIR(2)); // Red for unpushed
        }
        mvwprintw(viewer->commit_list_win, y, 2, "%s", viewer->commits[i].hash);
        if (viewer->commits[i].is_pushed) {
            wattroff(viewer->commit_list_win, COLOR_PAIR(1));
        } else {
            wattroff(viewer->commit_list_win, COLOR_PAIR(2));
        }
        
        // Show author initials with same color as hash
        if (viewer->commits[i].is_pushed) {
            wattron(viewer->commit_list_win, COLOR_PAIR(1)); // Green for pushed
        } else {
            wattron(viewer->commit_list_win, COLOR_PAIR(2)); // Red for unpushed
        }
        mvwprintw(viewer->commit_list_win, y, 10, "%s", viewer->commits[i].author_initials);
        if (viewer->commits[i].is_pushed) {
            wattroff(viewer->commit_list_win, COLOR_PAIR(1));
        } else {
            wattroff(viewer->commit_list_win, COLOR_PAIR(2));
        }
        
        // Show commit title (always white, truncated with better margins)
        int max_title_len = viewer->file_panel_width - 17; // More margin
        char truncated_title[256];
        if ((int)strlen(viewer->commits[i].title) > max_title_len) {
            strncpy(truncated_title, viewer->commits[i].title, max_title_len - 3);
            truncated_title[max_title_len - 3] = '\0';
            strcat(truncated_title, "...");
        } else {
            strcpy(truncated_title, viewer->commits[i].title);
        }
        
        mvwprintw(viewer->commit_list_win, y, 13, "%s", truncated_title);
    }
    
    wrefresh(viewer->commit_list_win);
}

/**
 * Render the file content window
 */
void render_file_content_window(NCursesDiffViewer *viewer) {
    if (!viewer || !viewer->file_content_win) return;
    
    // Draw rounded border
    draw_rounded_box(viewer->file_content_win);
    
    if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
        // Show file content (preview in list mode, scrollable in view mode)
        if (viewer->current_mode == MODE_FILE_LIST) {
            mvwprintw(viewer->file_content_win, 0, 2, " 2. %s (Preview) ", viewer->files[viewer->selected_file].filename);
        } else {
            mvwprintw(viewer->file_content_win, 0, 2, " 2. %s (Scrollable) ", viewer->files[viewer->selected_file].filename);
        }
        
        int max_lines_visible = viewer->terminal_height - 4;
        int content_width = viewer->terminal_width - viewer->file_panel_width - 3;
        
        // Clear all content lines first
        for (int i = 1; i <= max_lines_visible + 1; i++) {
            wmove(viewer->file_content_win, i, 1);
            wclrtoeol(viewer->file_content_win);
        }
        
        for (int i = 0; i < max_lines_visible && 
             (i + viewer->file_scroll_offset) < viewer->file_line_count; i++) {
            
            int line_idx = i + viewer->file_scroll_offset;
            NCursesFileLine *line = &viewer->file_lines[line_idx];
            
            int y = i + 1;
            
            // Apply color based on line type
            if (line->is_diff_line) {
                if (line->type == '+') {
                    wattron(viewer->file_content_win, COLOR_PAIR(1));
                } else if (line->type == '-') {
                    wattron(viewer->file_content_win, COLOR_PAIR(2));
                }
            }
            
            // Truncate line to fit window
            char display_line[1024];
            if ((int)strlen(line->line) > content_width - 2) {
                strncpy(display_line, line->line, content_width - 5);
                display_line[content_width - 5] = '\0';
                strcat(display_line, "...");
            } else {
                strcpy(display_line, line->line);
            }
            
            mvwprintw(viewer->file_content_win, y, 1, "%s", display_line);
            
            // Reset color
            if (line->is_diff_line) {
                if (line->type == '+') {
                    wattroff(viewer->file_content_win, COLOR_PAIR(1));
                } else if (line->type == '-') {
                    wattroff(viewer->file_content_win, COLOR_PAIR(2));
                }
            }
        }
        
        // Show scroll indicator
        if (viewer->file_line_count > max_lines_visible) {
            int scroll_pos = (viewer->file_scroll_offset * max_lines_visible) / viewer->file_line_count;
            mvwprintw(viewer->file_content_win, scroll_pos + 1, content_width + 1, "█");
        }
        
        // Show current position info only in file view mode
        if (viewer->current_mode == MODE_FILE_VIEW) {
            mvwprintw(viewer->file_content_win, max_lines_visible + 1, 2, 
                     "Line %d-%d of %d", 
                     viewer->file_scroll_offset + 1,
                     viewer->file_scroll_offset + max_lines_visible < viewer->file_line_count ? 
                         viewer->file_scroll_offset + max_lines_visible : viewer->file_line_count,
                     viewer->file_line_count);
        } else {
            mvwprintw(viewer->file_content_win, max_lines_visible + 1, 2, 
                     "Press Enter to enable scrolling");
        }
    }
    
    wrefresh(viewer->file_content_win);
}

/**
 * Handle keyboard input for navigation
 */
int handle_ncurses_diff_input(NCursesDiffViewer *viewer, int key) {
    if (!viewer) return 0;
    
    int max_lines_visible = viewer->terminal_height - 4;
    
    // Global quit commands
    if (key == 'q' || key == 'Q') {
        return 0; // Exit
    }
    
    // Global number key navigation
    switch (key) {
        case '1':
            viewer->current_mode = MODE_FILE_LIST;
            break;
        case '2':
            // Switch to file view mode and load selected file
            if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
                load_full_file_with_diff(viewer, viewer->files[viewer->selected_file].filename);
                viewer->current_mode = MODE_FILE_VIEW;
            }
            break;
        case '3':
            viewer->current_mode = MODE_COMMIT_LIST;
            break;
    }
    
    if (viewer->current_mode == MODE_FILE_LIST) {
        // File list mode navigation
        switch (key) {
            case 27: // ESC
                return 0; // Exit from file list mode
                
            case KEY_UP:
            case 'k':
                if (viewer->selected_file > 0) {
                    viewer->selected_file--;
                    // Auto-preview the selected file
                    if (viewer->file_count > 0) {
                        load_full_file_with_diff(viewer, viewer->files[viewer->selected_file].filename);
                    }
                }
                break;
                
            case KEY_DOWN:
            case 'j':
                if (viewer->selected_file < viewer->file_count - 1) {
                    viewer->selected_file++;
                    // Auto-preview the selected file
                    if (viewer->file_count > 0) {
                        load_full_file_with_diff(viewer, viewer->files[viewer->selected_file].filename);
                    }
                }
                break;
                
            case ' ': // Space - toggle file marking
                if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
                    toggle_file_mark(viewer, viewer->selected_file);
                }
                break;
                
            case 'a':
            case 'A': // Mark all files
                mark_all_files(viewer);
                break;
                
            case 'c':
            case 'C': // Commit marked files
                {
                    char commit_title[MAX_COMMIT_TITLE_LEN];
                    if (get_commit_title_input(commit_title, MAX_COMMIT_TITLE_LEN)) {
                        commit_marked_files(viewer, commit_title);
                    }
                }
                break;
                
            case '\t': // Tab - switch to commit list mode
                viewer->current_mode = MODE_COMMIT_LIST;
                break;
                
            case '\n':
            case '\r':
            case KEY_ENTER:
                // Enter file view mode and load the selected file
                if (viewer->file_count > 0 && viewer->selected_file < viewer->file_count) {
                    load_full_file_with_diff(viewer, viewer->files[viewer->selected_file].filename);
                    viewer->current_mode = MODE_FILE_VIEW;
                }
                break;
        }
    } else if (viewer->current_mode == MODE_FILE_VIEW) {
        // File view mode navigation
        switch (key) {
            case 27: // ESC
                viewer->current_mode = MODE_FILE_LIST; // Return to file list mode
                break;
                
            case KEY_UP:
            case 'k':
                // Scroll content up
                if (viewer->file_scroll_offset > 0) {
                    viewer->file_scroll_offset--;
                }
                break;
                
            case KEY_DOWN:
            case 'j':
                // Scroll content down
                if (viewer->file_line_count > max_lines_visible && 
                    viewer->file_scroll_offset < viewer->file_line_count - max_lines_visible) {
                    viewer->file_scroll_offset++;
                }
                break;
                
            case 21: // Ctrl+U
                // Scroll up 30 lines
                viewer->file_scroll_offset -= 30;
                if (viewer->file_scroll_offset < 0) {
                    viewer->file_scroll_offset = 0;
                }
                break;
                
            case 4: // Ctrl+D
                // Scroll down 30 lines
                if (viewer->file_line_count > max_lines_visible) {
                    viewer->file_scroll_offset += 30;
                    if (viewer->file_scroll_offset > viewer->file_line_count - max_lines_visible) {
                        viewer->file_scroll_offset = viewer->file_line_count - max_lines_visible;
                    }
                }
                break;
                
            case KEY_NPAGE: // Page Down
            case ' ':
                // Scroll content down by page
                if (viewer->file_line_count > max_lines_visible) {
                    viewer->file_scroll_offset += max_lines_visible;
                    if (viewer->file_scroll_offset > viewer->file_line_count - max_lines_visible) {
                        viewer->file_scroll_offset = viewer->file_line_count - max_lines_visible;
                    }
                }
                break;
                
            case KEY_PPAGE: // Page Up
                // Scroll content up by page
                viewer->file_scroll_offset -= max_lines_visible;
                if (viewer->file_scroll_offset < 0) {
                    viewer->file_scroll_offset = 0;
                }
                break;
        }
    } else if (viewer->current_mode == MODE_COMMIT_LIST) {
        // Commit list mode navigation
        switch (key) {
            case 27: // ESC
            case '\t': // Tab - return to file list mode
                viewer->current_mode = MODE_FILE_LIST;
                break;
                
            case KEY_UP:
            case 'k':
                if (viewer->selected_commit > 0) {
                    viewer->selected_commit--;
                }
                break;
                
            case KEY_DOWN:
            case 'j':
                if (viewer->selected_commit < viewer->commit_count - 1) {
                    viewer->selected_commit++;
                }
                break;
                
            case 'P': // Push commit
                if (viewer->commit_count > 0 && viewer->selected_commit < viewer->commit_count) {
                    push_commit(viewer, viewer->selected_commit);
                }
                break;
        }
    }
    
    return 1; // Continue
}

/**
 * Run the ncurses diff viewer
 */
int run_ncurses_diff_viewer(void) {
    NCursesDiffViewer viewer;
    
    if (!init_ncurses_diff_viewer(&viewer)) {
        printf("Failed to initialize ncurses diff viewer\n");
        return 1;
    }
    
    if (get_ncurses_changed_files(&viewer) == 0) {
        cleanup_ncurses_diff_viewer(&viewer);
        printf("No changed files found\n");
        return 1;
    }
    
    // Load commit history
    get_commit_history(&viewer);
    
    // Load the first file by default
    if (viewer.file_count > 0) {
        load_full_file_with_diff(&viewer, viewer.files[0].filename);
    }
    
    // Initial display
    attron(COLOR_PAIR(3));
    if (viewer.current_mode == MODE_FILE_LIST) {
        mvprintw(0, 0, "Git Diff Viewer: 1=files 2=view 3=commits | j/k=nav Space=mark A=all C=commit P=push | q=quit");
    } else if (viewer.current_mode == MODE_FILE_VIEW) {
        mvprintw(0, 0, "Git Diff Viewer: 1=files 2=view 3=commits | j/k=scroll Ctrl+U/D=30lines | q=quit");
    } else {
        mvprintw(0, 0, "Git Diff Viewer: 1=files 2=view 3=commits | j/k=nav P=push | q=quit");
    }
    attroff(COLOR_PAIR(3));
    refresh();
    
    render_file_list_window(&viewer);
    render_file_content_window(&viewer);
    render_commit_list_window(&viewer);
    
    // Main display loop
    int running = 1;
    NCursesViewMode last_mode = viewer.current_mode;
    
    while (running) {
        // Only update title if mode changed
        if (viewer.current_mode != last_mode) {
            // Clear just the title line
            move(0, 0);
            clrtoeol();
            
            attron(COLOR_PAIR(3));
            if (viewer.current_mode == MODE_FILE_LIST) {
                mvprintw(0, 0, "Git Diff Viewer: 1=files 2=view 3=commits | j/k=nav Space=mark A=all C=commit P=push | q=quit");
            } else if (viewer.current_mode == MODE_FILE_VIEW) {
                mvprintw(0, 0, "Git Diff Viewer: 1=files 2=view 3=commits | j/k=scroll Ctrl+U/D=30lines | q=quit");
            } else {
                mvprintw(0, 0, "Git Diff Viewer: 1=files 2=view 3=commits | j/k=nav P=push | q=quit");
            }
            attroff(COLOR_PAIR(3));
            refresh();
            last_mode = viewer.current_mode;
        }
        
        render_file_list_window(&viewer);
        render_file_content_window(&viewer);
        render_commit_list_window(&viewer);
        
        int c = getch();
        running = handle_ncurses_diff_input(&viewer, c);
    }
    
    cleanup_ncurses_diff_viewer(&viewer);
    return 0;
}

/**
 * Clean up ncurses diff viewer resources
 */
void cleanup_ncurses_diff_viewer(NCursesDiffViewer *viewer) {
    if (viewer) {
        if (viewer->file_list_win) {
            delwin(viewer->file_list_win);
        }
        if (viewer->file_content_win) {
            delwin(viewer->file_content_win);
        }
        if (viewer->commit_list_win) {
            delwin(viewer->commit_list_win);
        }
    }
    endwin();
}