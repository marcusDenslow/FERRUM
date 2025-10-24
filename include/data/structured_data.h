
#ifndef STRUCTURED_DATA_H
#define STRUCTURED_DATA_H

#include "common.h"

// Value types for structured data
typedef enum {
    TYPE_STRING,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_SIZE    // Special type for file sizes with units
} ValueType;

// A single data value that can be of different types
typedef struct {
    ValueType type;
    union {
        char *str_val;
        int int_val;
        float float_val;
        struct {
            double value;
            char unit[8];
        } size_val;
    } value;
    int is_highlighted;  // Flag for highlighting in tables
} DataValue;

// Table structure to hold rows and columns of data
typedef struct {
    char **headers;        // Column names
    int header_count;      // Number of columns
    DataValue **rows;      // Array of rows (each row is an array of DataValues)
    int row_count;         // Current number of rows
    int row_capacity;      // Allocated capacity for rows
} TableData;

// Function to create a new table with the given headers
TableData* create_table(char **headers, int header_count);

// Function to add a row to a table
void add_table_row(TableData *table, DataValue *row);

// Function to free all memory associated with a table
void free_table(TableData *table);

// Function to create a copy of a DataValue
DataValue copy_data_value(const DataValue *src);

// Function to free a DataValue
void free_data_value(DataValue *value);

// Function to filter a table based on a condition
TableData* filter_table(TableData *input, char *field, char *op, char *value);

// Function to print a table to the console
void print_table(TableData *table);

// Function to parse human-readable sizes
long parse_size(const char *size_str);

// Function to extract size in bytes from a formatted size string
long extract_size_bytes(const char *size_str);

#endif // STRUCTURED_DATA_H
