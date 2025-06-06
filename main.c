// brih
/**
 * main.c
 * Entry point for the LSH shell program
 */

#include "common.h"
#include "shell.h"
#include <locale.h>

/**
 * Main entry point
 */

int main(int argc, char **argv) {
  // Set locale to support UTF-8
  setlocale(LC_ALL, "en_US.UTF-8");

  // Setup terminal for proper UTF-8 handling
  // This is Linux equivalent of SetConsoleOutputCP(65001)
  printf("\033%%G"); // Tell terminal to use UTF-8

  // Start the shell loop
  lsh_loop();

  return EXIT_SUCCESS;
}
