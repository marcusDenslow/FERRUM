/**
 * fzf_native.h
 * Header for native fzf command-line fuzzy finder wrapper
 */

#ifndef FZF_NATIVE_H
#define FZF_NATIVE_H

#include "builtins.h"
#include "common.h"

/**
 * Check if native fzf is installed on the system
 *
 * @return 1 if installed, 0 if not
 */
int is_fzf_installed(void);

/**
 * Display instructions for installing fzf
 */
void show_fzf_install_instructions(void);

/**
 * Run fzf with files from the current directory
 *
 * @param preview Enable preview window if 1
 * @param args Additional arguments for fzf
 * @return Selected filename or NULL if canceled
 */
char *run_native_fzf_files(int preview, char **args);

/**
 * Run fzf with all files and directories from the current directory
 *
 * @param recursive Search recursively if 1
 * @param args Additional arguments for fzf
 * @return Selected path or NULL if canceled
 */
char *run_native_fzf_all(int recursive, char **args);

/**
 * Run fzf with command history
 *
 * @return Selected command or NULL if canceled
 */
char *run_native_fzf_history(void);

/**
 * Command handler for the fzf command
 *
 * @param args Command arguments
 * @return 1 to continue shell execution, 0 to exit shell
 */
int lsh_fzf_native(char **args);

#endif // FZF_NATIVE_H
