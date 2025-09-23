/**
 * countdown_timer.h
 * Header for focus timer functionality
 */

#ifndef COUNTDOWN_TIMER_H
#define COUNTDOWN_TIMER_H

#include "common.h"

/**
 * Start a new countdown timer
 *
 * @param seconds Total duration in seconds
 * @param name Optional name for the timer session
 * @return 1 if successful, 0 on failure
 */
int start_countdown_timer(int seconds, const char *name);

/**
 * Stop the current countdown timer
 */
void stop_countdown_timer();

/**
 * Check if a timer is currently active
 *
 * @return TRUE if timer is active, FALSE otherwise
 */
BOOL is_timer_active();

/**
 * Get the current timer display text
 *
 * @return Pointer to the display text string
 */
const char *get_timer_display();

/**
 * Temporarily hide the timer display (used when running external programs)
 */
void hide_timer_display(void);

/**
 * Restore the timer display after it was hidden
 */
void show_timer_display(void);

/**
 * Command handler for the "timer" command
 *
 * @param args Command arguments
 * @return 1 to continue shell, 0 to exit
 */
int lsh_focus_timer(char **args);

#endif // COUNTDOWN_TIMER_H
