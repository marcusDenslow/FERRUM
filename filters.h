/**
 * filters.h
 * Declarations for filter commands (where, sort-by, etc.)
 */

#ifndef FILTERS_H
#define FILTERS_H

#include "structured_data.h"

/**
 * Filter a table based on a condition (e.g., where size > 10kb)
 * 
 * @param input The input table
 * @param args Command arguments
 * @return Filtered table or NULL on error
 */
TableData* lsh_where(TableData *input, char **args);

/**
 * Sort a table based on a column (e.g., sort-by size desc)
 * 
 * @param input The input table
 * @param args Command arguments
 * @return Sorted table or NULL on error
 */
TableData* lsh_sort_by(TableData *input, char **args);

/**
 * Select specific columns from a table
 * 
 * @param input The input table
 * @param args Command arguments
 * @return Table with selected columns or NULL on error
 */
TableData* lsh_select(TableData *input, char **args);

/**
 * Filter rows where a column contains a substring
 * 
 * @param input The input table
 * @param args Command arguments
 * @return Filtered table or NULL on error
 */
TableData* lsh_contains(TableData *input, char **args);

/**
 * Limit the number of rows displayed
 * 
 * @param input The input table
 * @param args Command arguments
 * @return Limited table or NULL on error
 */
TableData* lsh_limit(TableData *input, char **args);

/**
 * Case-insensitive substring search (strcasestr equivalent for Windows)
 */
char *my_strcasestr(const char *haystack, const char *needle);

// Export the filter arrays for the shell
extern char *filter_str[];
extern TableData* (*filter_func[]) (TableData*, char**);
extern int filter_count;

#endif // FILTERS_H
