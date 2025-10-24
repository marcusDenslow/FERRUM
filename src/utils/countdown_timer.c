
#include "countdown_timer.h"
#include "common.h"
#include "shell.h"
#include "themes.h"
#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Global timer state
static struct {
  BOOL is_active;             // Whether timer is currently running
  time_t end_time;            // When timer will expire (in seconds since epoch)
  char display_text[64];      // Current timer display text
  char session_name[128];     // Optional session name
  pthread_t timer_thread;     // Timer thread ID
  BOOL should_exit;           // Flag to signal thread exit
  BOOL is_temporarily_hidden; // Flag to hide timer while running external
                              // programs
} timer_state = {FALSE, 0, "", "", 0, FALSE, FALSE};

static void update_timer_display(void) {
  if (!timer_state.is_active) {
    strcpy(timer_state.display_text, "");
    return;
  }

  // Calculate remaining time
  time_t now = time(NULL);
  int seconds_left = (int)difftime(timer_state.end_time, now);

  if (seconds_left <= 0) {
    // Timer has expired
    if (strlen(timer_state.session_name) > 0) {
      snprintf(timer_state.display_text, sizeof(timer_state.display_text),
               "Session '%s' complete!", timer_state.session_name);
    } else {
      strcpy(timer_state.display_text, "Timer complete!");
    }
    return;
  }

  // Format time as MM:SS
  int minutes = seconds_left / 60;
  int seconds = seconds_left % 60;

  if (strlen(timer_state.session_name) > 0) {
    snprintf(timer_state.display_text, sizeof(timer_state.display_text),
             "%s: %02d:%02d remaining", timer_state.session_name, minutes,
             seconds);
  } else {
    snprintf(timer_state.display_text, sizeof(timer_state.display_text),
             "Focus: %02d:%02d remaining", minutes, seconds);
  }
}

static void *timer_thread_func(void *param) {
  while (!timer_state.should_exit) {
    // Update timer display
    update_timer_display();

    // Check if timer has expired
    time_t now = time(NULL);
    if (difftime(timer_state.end_time, now) <= 0) {
      // Play a bell sound
      printf("\a");
      fflush(stdout);

      // Update status bar with completion message
      update_timer_display();

      // Keep thread running to display completion message
    }

    // Sleep for 1 second
    sleep(1);
  }

  return NULL;
}

int start_countdown_timer(int seconds, const char *name) {
  // Stop any existing timer
  if (timer_state.is_active) {
    stop_countdown_timer();
  }

  // Set timer parameters
  timer_state.end_time = time(NULL) + seconds;
  timer_state.is_active = TRUE;
  timer_state.should_exit = FALSE;
  timer_state.is_temporarily_hidden = FALSE;

  // Set optional session name
  if (name && *name) {
    strncpy(timer_state.session_name, name,
            sizeof(timer_state.session_name) - 1);
    timer_state.session_name[sizeof(timer_state.session_name) - 1] = '\0';
  } else {
    timer_state.session_name[0] = '\0';
  }

  // Initialize display text
  update_timer_display();

  // Create timer thread
  int ret =
      pthread_create(&timer_state.timer_thread, NULL, timer_thread_func, NULL);
  if (ret != 0) {
    fprintf(stderr, "Failed to create timer thread: %s\n", strerror(ret));
    timer_state.is_active = FALSE;
    return 0;
  }

  // Detach thread so it cleans up automatically
  pthread_detach(timer_state.timer_thread);

  return 1;
}

void stop_countdown_timer() {
  if (!timer_state.is_active) {
    return;
  }

  // Signal thread to exit
  timer_state.should_exit = TRUE;

  // Wait for thread to terminate (with timeout)
  // Use a more portable approach since pthread_timedjoin_np is GNU-specific

  // Set a timeout of 2 seconds
  struct timespec timeout;
  timeout.tv_sec = 2;
  timeout.tv_nsec = 0;

  // Sleep for the timeout period - the thread will keep running
  // and clean itself up since we detached it
  nanosleep(&timeout, NULL);

  // Reset timer state
  timer_state.is_active = FALSE;
  timer_state.display_text[0] = '\0';
  timer_state.session_name[0] = '\0';
}

BOOL is_timer_active() { return timer_state.is_active; }

const char *get_timer_display() {
  if (timer_state.is_temporarily_hidden || !timer_state.is_active) {
    return "";
  }
  return timer_state.display_text;
}

void hide_timer_display(void) { timer_state.is_temporarily_hidden = TRUE; }

void show_timer_display(void) { timer_state.is_temporarily_hidden = FALSE; }

static int parse_time_string(const char *time_str) {
  if (!time_str || !*time_str) {
    return 0;
  }

  int total_seconds = 0;
  char *str = strdup(time_str);
  if (!str) {
    return 0;
  }

  char *pos = str;
  char *next_pos;
  int value;

  while (*pos) {
    // Skip non-digits
    while (*pos && !isdigit(*pos)) {
      pos++;
    }

    if (!*pos) {
      break;
    }

    // Parse number
    value = (int)strtol(pos, &next_pos, 10);
    if (pos == next_pos) {
      break;
    }

    // Skip whitespace
    pos = next_pos;
    while (*pos && isspace(*pos)) {
      pos++;
    }

    // Check unit
    if (*pos == 'h' || *pos == 'H') {
      total_seconds += value * 3600;
    } else if (*pos == 'm' || *pos == 'M') {
      total_seconds += value * 60;
    } else if (*pos == 's' || *pos == 'S') {
      total_seconds += value;
    } else {
      // No unit specified, assume minutes
      total_seconds += value * 60;
    }

    // Move to next part
    if (*pos) {
      pos++;
    }
  }

  free(str);
  return total_seconds;
}

int lsh_focus_timer(char **args) {
  if (!args[1]) {
    // No arguments - show help
    printf("Usage: focus_timer [start|stop] [duration] [session name]\n");
    printf("Examples:\n");
    printf("  focus_timer start 25m \"Coding session\"   # Start a 25-minute "
           "timer\n");
    printf("  focus_timer start 1h30m                  # Start a 1 hour 30 "
           "minute timer\n");
    printf("  focus_timer stop                         # Stop the current "
           "timer\n");
    printf("Current status: %s\n", timer_state.is_active
                                       ? timer_state.display_text
                                       : "No active timer");
    return 1;
  }

  if (strcmp(args[1], "stop") == 0) {
    if (timer_state.is_active) {
      printf("Stopping timer...\n");
      stop_countdown_timer();
    } else {
      printf("No active timer to stop.\n");
    }
    return 1;
  }

  if (strcmp(args[1], "start") == 0) {
    if (!args[2]) {
      printf("Error: Duration required. Example: focus_timer start 25m\n");
      return 1;
    }

    int seconds = parse_time_string(args[2]);
    if (seconds <= 0) {
      printf("Error: Invalid duration format. Examples: 25m, 1h30m, 90s\n");
      return 1;
    }

    const char *session_name = args[3] ? args[3] : "";
    if (start_countdown_timer(seconds, session_name)) {
      printf("Timer started: %s\n", timer_state.display_text);
    } else {
      printf("Failed to start timer.\n");
    }
    return 1;
  }

  printf("Unknown timer command: %s\n", args[1]);
  return 1;
}
