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
int prepend_dir(void);
int process_command(char *command, int *should_exit);
char *get_current_dir(void);
char *executable_search(const char *exec);

#endif