#include "common.h"
#include "shell.h"
#include <locale.h>
// this is a test

int main(int argc, char **argv) {
  setlocale(LC_ALL, "en_US.UTF-8");
  printf("\033%%G");
  lsh_loop();
  return EXIT_SUCCESS;
}
