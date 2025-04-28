/**
 * grep.h
 * Header for fast text searching in files with fuzzy matching capabilities
 */

#ifndef GREP_H
#define GREP_H

#include "common.h"

/**
 * Command handler for the "grep" command
 *
 * Usage: grep [options] pattern [file/directory]
 * Options:
 *   -n, --line-numbers  Show line numbers
 *   -i, --ignore-case   Ignore case distinctions
 *   -r, --recursive     Search directories recursively
 *   -f, --fuzzy         Use fuzzy matching instead of exact
 *   --file              Specify files/directories to search (otherwise searches
 * current dir)
 *
 * @param args Command arguments
 * @return 1 to continue shell execution, 0 to exit shell
 */
int lsh_grep(char **args);

#endif // GREP_H
