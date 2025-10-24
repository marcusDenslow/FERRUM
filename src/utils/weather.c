
#include "weather.h"
#include "favorite_cities.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// this is a test
// this is another test

int lsh_weather(char **args) {
  char command[256];
  char location[128] = ""; // Default empty location

  // Check if a location was provided as an argument
  if (args[1]) {
    // Use the provided location
    strncpy(location, args[1], sizeof(location) - 1);
    location[sizeof(location) - 1] = '\0';
  } else {
    // No location provided, use current location (blank parameter to wttr.in)
    location[0] = '\0';
  }

  // Properly URL-encode spaces in the location
  char encoded_location[256] = "";
  char *src = location;
  char *dst = encoded_location;

  while (*src) {
    if (*src == ' ') {
      *dst++ = '%';
      *dst++ = '2';
      *dst++ = '0';
    } else {
      *dst++ = *src;
    }
    src++;
  }
  *dst = '\0';

  // Format command with the location - using default wttr.in display format
  snprintf(command, sizeof(command), "curl -s wttr.in/%s", encoded_location);

  // Execute the command and display the full wttr.in UI
  system(command);

  return 1;
}
