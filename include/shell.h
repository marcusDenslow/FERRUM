
#ifndef SHELL_H
#define SHELL_H

#include "common.h"
#include "structured_data.h"

void display_welcome_banner(void);

int lsh_execute(char **args);

int lsh_execute_piped(char ***commands);

TableData* create_ls_table(char **args);

char*** lsh_split_commands(char *line);

int lsh_launch(char **args);

void lsh_loop(void);

void free_commands(char ***commands);

int init_status_bar(int fd);

void hide_status_bar(int fd);

void ensure_status_bar_space(int fd);

void check_console_resize(int fd);

void update_status_bar(int fd, const char *git_info);

void get_path_display(const char *cwd, char *parent_dir_name,
                      char *current_dir_name, size_t buf_size);

int init_terminal(struct termios *orig_termios);

void restore_terminal(int fd, struct termios *orig_termios);

int get_console_dimensions(int fd, int *width, int *height);

#endif // SHELL_H