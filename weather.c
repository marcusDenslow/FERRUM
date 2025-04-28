/**
 * weather.c
 * Implementation of weather information fetching and display for Linux
 * This is a simplified version that provides basic information about using curl/wget
 * instead of implementing full HTTP functionality
 */

#include "weather.h"
#include "favorite_cities.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Command handler for the "weather" command
 * In this Linux version, we'll provide instructions for using curl/wget
 */
int lsh_weather(char **args) {
  printf("\n");
  printf("╭─────────────────────────────────────────────────────────────╮\n");
  printf("│              Weather Command (Linux Version)                 │\n");
  printf("├─────────────────────────────────────────────────────────────┤\n");
  printf("│ This is a simplified version of the weather command for     │\n");
  printf("│ Linux. The full version would use libcurl for HTTP requests. │\n");
  printf("│                                                             │\n");
  printf("│ To get weather information on Linux:                        │\n");
  printf("│                                                             │\n");
  printf("│ 1. Using curl:                                              │\n");
  printf("│    curl wttr.in/[city]                                      │\n");
  printf("│                                                             │\n");
  printf("│ 2. Using wget:                                              │\n");
  printf("│    wget -O - wttr.in/[city]                                 │\n");
  printf("│                                                             │\n");
  printf("│ For example:                                                │\n");
  printf("│    curl wttr.in/London                                      │\n");
  printf("│    curl wttr.in/\"New York\"                                  │\n");
  printf("│                                                             │\n");
  printf("│ For a shorter format:                                       │\n");
  printf("│    curl wttr.in/London?format=3                             │\n");
  printf("│                                                             │\n");
  printf("├─────────────────────────────────────────────────────────────┤\n");
  
  // Check if a city was provided as an argument
  if (args[1]) {
    char command[256];
    // Use wttr.in for a simple demonstration
    snprintf(command, sizeof(command), "curl -s wttr.in/%s?format=3", args[1]);
    printf("│ Running: %-47s │\n", command);
    printf("├─────────────────────────────────────────────────────────────┤\n");
    printf("│ Result:                                                     │\n");
    printf("│                                                             │\n");
    
    // Execute the command
    FILE *fp = popen(command, "r");
    if (fp) {
      char buffer[256];
      if (fgets(buffer, sizeof(buffer), fp)) {
        // Print the result with padding
        printf("│   %-53s │\n", buffer);
      } else {
        printf("│   No result returned                                      │\n");
      }
      pclose(fp);
    } else {
      printf("│   Error executing command                                 │\n");
    }
    
    printf("│                                                             │\n");
  }
  
  printf("╰─────────────────────────────────────────────────────────────╯\n\n");
  
  return 1;
}