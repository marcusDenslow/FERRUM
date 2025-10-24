
#include "builtins.h"
#include "common.h"
#include "structured_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TableData* lsh_ps_structured(char **args) {
    // Define our table headers
    char *headers[] = {"PID", "Name", "Memory", "Threads"};
    int header_count = 4;
    
    // Create our table
    TableData *table = create_table(headers, header_count);
    if (!table) {
        fprintf(stderr, "lsh: allocation error in ps_structured\n");
        return NULL;
    }
    
    // Use system's ps command to get process info
    FILE *fp = popen("ps -e -o pid,comm,vsz,nlwp --no-headers", "r");
    if (!fp) {
        fprintf(stderr, "lsh: failed to execute ps command\n");
        free_table(table);
        return NULL;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        // Parse the line
        char name[256] = {0};
        unsigned long pid = 0, vsz = 0, threads = 0;
        
        sscanf(line, "%lu %255s %lu %lu", &pid, name, &vsz, &threads);
        
        // Create a new row for this process
        DataValue *row = (DataValue*)malloc(header_count * sizeof(DataValue));
        if (!row) {
            fprintf(stderr, "lsh: allocation error in ps_structured\n");
            free_table(table);
            pclose(fp);
            return NULL;
        }
        
        // Set PID (as a string for compatibility)
        char pidStr[20];
        sprintf(pidStr, "%lu", pid);
        row[0].type = TYPE_STRING;
        row[0].value.str_val = strdup(pidStr);
        
        // Set process name
        row[1].type = TYPE_STRING;
        row[1].value.str_val = strdup(name);
        
        // Format memory usage string (important for filtering)
        char memoryString[32];
        if (vsz < 1024) {
            sprintf(memoryString, "%lu B", vsz);
        } else if (vsz < 1024 * 1024) {
            sprintf(memoryString, "%.1f KB", vsz / 1024.0);
        } else {
            // Format as MB for consistency in filtering
            sprintf(memoryString, "%.1f MB", vsz / (1024.0 * 1024.0));
        }
        
        row[2].type = TYPE_SIZE;  // Use the special SIZE type for filtering
        row[2].value.str_val = strdup(memoryString);
        
        // Set thread count
        char threadStr[20];
        sprintf(threadStr, "%lu", threads);
        row[3].type = TYPE_STRING;
        row[3].value.str_val = strdup(threadStr);
        
        // Add the row to the table
        add_table_row(table, row);
    }
    
    pclose(fp);
    return table;
}

int lsh_ps_fancy(char **args) {
    // Create structured data table for processes
    TableData *table = lsh_ps_structured(args);
    
    if (table) {
        // Print the table using our nice formatting
        print_table(table);
        
        // Free the table
        free_table(table);
    }
    
    return 1;
}