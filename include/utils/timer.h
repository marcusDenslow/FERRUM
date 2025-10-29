#ifndef TIMER_H
#define TIMER_H

#include "common.h"

void format_time(double ms, char *buffer, size_t buffer_size);
int lsh_timer(char **args);
int lsh_time(char **args);

#endif // TIMER_H
