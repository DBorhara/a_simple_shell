#ifndef MYSH_H
#define MYSH_H

#include <stddef.h>

// Struct definitions go here.
typedef struct
{
    char **args;
    size_t argc;
    char *input_file;
    char *output_file;
    int append;
} Command;

// Function declarations go here.
void report_command_status(int status, int signal_number);
int prepend_dir(void);
int process_command(char *command, int *should_exit, int batch_mode, int *signal_number);
char *get_current_dir(void);
char *executable_search(const char *exec);
int execute_builtin(Command *cmd, int *should_exit);

#endif