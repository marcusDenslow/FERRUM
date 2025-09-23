/**
 * timer.h
 * Header for command execution timer functionality
 */

#ifndef TIMER_H
#define TIMER_H

#include "common.h"

/**
 * Format a time interval in milliseconds to a readable string
 *
 * @param ms Time in milliseconds
 * @param buffer Buffer to store formatted time
 * @param buffer_size Size of the buffer
 */
void format_time(double ms, char *buffer, size_t buffer_size);

/**
 * Implementation of the timer command
 * Usage: timer COMMAND [ARGS...]
 *
 * @param args Command arguments
 * @return 1 to continue, 0 to exit
 */
int lsh_timer(char **args);

/**
 * Implementation of the time command (alias for timer)
 * Usage: time COMMAND [ARGS...]
 *
 * @param args Command arguments
 * @return 1 to continue, 0 to exit
 */
int lsh_time(char **args);

#endif // TIMER_H
