
#ifndef AUTOCORRECT_H
#define AUTOCORRECT_H

#include "common.h"
#include "line_reader.h"
#include <limits.h>

int levenshtein_distance(const char *s1, const char *s2);

int min3(int a, int b, int c);

void init_autocorrect(void);

void shutdown_autocorrect(void);

char **check_for_corrections(char **args);

int count_args(char **args);

#endif // AUTOCORRECT_H