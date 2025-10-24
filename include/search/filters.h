// this is a test

#ifndef FILTERS_H
#define FILTERS_H

#include "structured_data.h"

TableData *lsh_where(TableData *input, char **args);

TableData *lsh_sort_by(TableData *input, char **args);

TableData *lsh_select(TableData *input, char **args);

TableData *lsh_contains(TableData *input, char **args);

TableData *lsh_limit(TableData *input, char **args);

char *my_strcasestr(const char *haystack, const char *needle);

// Export the filter arrays for the shell
extern char *filter_str[];
extern TableData *(*filter_func[])(TableData *, char **);
extern int filter_count;

#endif // FILTERS_H
