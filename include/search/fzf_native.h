
#ifndef FZF_NATIVE_H
#define FZF_NATIVE_H

#include "builtins.h"
#include "common.h"

int is_fzf_installed(void);

void show_fzf_install_instructions(void);

char *run_native_fzf_files(int preview, char **args);

char *run_native_fzf_all(int recursive, char **args);

char *run_native_fzf_history(void);

int lsh_fzf_native(char **args);

#endif // FZF_NATIVE_H
