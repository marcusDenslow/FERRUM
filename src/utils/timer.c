#include "utils/timer.h"
#include "builtins.h"
#include "shell.h"
#include <time.h>

void format_time(double ms, char *buffer, size_t buffer_size) {
  if (ms < 1.0) {
    snprintf(buffer, buffer_size, "%.2f μs", ms * 1000);
  } else if (ms < 1000.0) {
    snprintf(buffer, buffer_size, "%.2f ms", ms);
  } else if (ms < 60000.0) {
    snprintf(buffer, buffer_size, "%.2f s", ms / 1000);
  } else {
    int minutes = (int)(ms / 60000);
    double seconds = (ms - minutes * 60000) / 1000;
    snprintf(buffer, buffer_size, "%d min %.2f s", minutes, seconds);
  }
}

int lsh_timer(char **args) {
  if (!args[1]) {
    fprintf(stderr, "timer: usage: timer COMMAND [ARGS...]\n");
    return 1;
  }

  char **cmd_args = &args[1];

  // Don't time commands that affect shell state
  if (strcmp(cmd_args[0], "cd") == 0 || strcmp(cmd_args[0], "exit") == 0 ||
      strcmp(cmd_args[0], "timer") == 0) {
    fprintf(stderr, "timer: can't time built-in command: %s\n", cmd_args[0]);
    return 1;
  }

  clock_t start = clock();
  int result = lsh_execute(cmd_args);
  clock_t end = clock();

  double ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000;

  char time_str[64];
  format_time(ms, time_str, sizeof(time_str));

  printf("\n" ANSI_COLOR_GREEN);
  printf("╭───────────────────────────────────╮\n");
  printf("│ Execution time: %-18s │\n", time_str);
  printf("╰───────────────────────────────────╯\n");
  printf(ANSI_COLOR_RESET);

  return result;
}

int lsh_time(char **args) { return lsh_timer(args); }
