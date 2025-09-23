/**
 * line_reader.h
 * Functions for reading and parsing command lines
 */

#ifndef LINE_READER_H
#define LINE_READER_H

#include "common.h"

/**
 * Check if a command is valid
 *
 * @param cmd The command to check
 * @return 1 if valid, 0 if not
 */
int is_valid_command(const char *cmd);

/**
 * Read a key from the terminal
 * 
 * Handles special keys and escape sequences
 */
int read_key(void);

/**
 * Read a line of input from the user with tab completion
 *
 * @return The line read from stdin (must be freed by caller)
 */
char *lsh_read_line(void);

void generate_enhanced_prompt(char *prompt_buffer, size_t buffer_size);

/**
 * Split a line into tokens
 *
 * @param line The line to split
 * @return An array of tokens (must be freed by caller)
 */
char **lsh_split_line(char *line);

/**
 * Parse a token from a string, handling quoted strings
 *
 * @param str_ptr Pointer to the string to parse, will be updated
 * @return The parsed token, or NULL if no more tokens
 */
char *parse_token(char **str_ptr);

/**
 * Split a line with pipes into separate command arrays
 *
 * @param line The line to split
 * @return NULL-terminated array of NULL-terminated token arrays
 */
char ***lsh_split_piped_line(char *line);

#endif // LINE_READER_H
