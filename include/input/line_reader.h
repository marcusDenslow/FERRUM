
#ifndef LINE_READER_H
#define LINE_READER_H

#include "common.h"

int is_valid_command(const char *cmd);

int read_key(void);

char *lsh_read_line(void);

void generate_enhanced_prompt(char *prompt_buffer, size_t buffer_size);

char **lsh_split_line(char *line);

char *parse_token(char **str_ptr);

char ***lsh_split_piped_line(char *line);

#endif // LINE_READER_H
