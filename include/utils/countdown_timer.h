
#ifndef COUNTDOWN_TIMER_H
#define COUNTDOWN_TIMER_H

#include "common.h"

int start_countdown_timer(int seconds, const char *name);

void stop_countdown_timer();

BOOL is_timer_active();

const char *get_timer_display();

void hide_timer_display(void);

void show_timer_display(void);

int lsh_focus_timer(char **args);

#endif // COUNTDOWN_TIMER_H
