/**
 * autocorrect.h
 * Header for command auto-correction functionality
 */

#ifndef AUTOCORRECT_H
#define AUTOCORRECT_H

#include "common.h"
#include "line_reader.h"
#include <limits.h>

/**
 * Calculate Levenshtein distance between two strings
 *
 * @param s1 First string
 * @param s2 Second string
 * @return Edit distance between the strings
 */
int levenshtein_distance(const char *s1, const char *s2);

/**
 * Helper function to find minimum of three integers
 */
int min3(int a, int b, int c);

/**
 * Initialize the autocorrect system
 */
void init_autocorrect(void);

/**
 * Shutdown the autocorrect system
 */
void shutdown_autocorrect(void);

/**
 * Check a command for possible corrections
 * 
 * @param args Command arguments
 * @return Corrected arguments array (must be freed by caller) or NULL if no correction
 */
char **check_for_corrections(char **args);

/**
 * Count the number of arguments in a NULL-terminated array
 */
int count_args(char **args);

#endif // AUTOCORRECT_H