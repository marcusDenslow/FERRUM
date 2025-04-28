/**
 * grep.c
 * Simple implementation of text searching for Linux
 */

#include "grep.h"
#include "builtins.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <strings.h>

/**
 * Simple grep implementation
 */
int lsh_actual_grep(char **args) {
    if (args[1] == NULL) {
        printf("Usage: grep [pattern] [file...]\n");
        return 1;
    }

    // Use the system's grep command
    char command[1024] = "grep ";
    
    // Add the pattern (first argument)
    strcat(command, "\"");
    strcat(command, args[1]);
    strcat(command, "\"");
    
    // Add any additional files specified
    for (int i = 2; args[i] != NULL; i++) {
        strcat(command, " ");
        strcat(command, args[i]);
    }
    
    // If no files specified, use all files in current directory
    if (args[2] == NULL) {
        strcat(command, " .");
    }
    
    // Execute the grep command
    return system(command);
}

/**
 * Wrapper for grep
 */
int lsh_grep(char **args) {
    return lsh_actual_grep(args);
}