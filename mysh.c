#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "mysh.h"

int main(int argc, char const *argv[])
{
    if (argc > 2)
    {
        char msg[] = "No more than 1 argument allowed.\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        exit(EXIT_FAILURE);
    }
    int should_exit = 0;
    int last_status = 0;
    ssize_t bytes;
    size_t buf_capacity = BUFSIZ;
    char *buf = malloc(buf_capacity);
    if (buf == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    size_t buf_len = 0;
    // Batch fd or interactive mode fd
    int fd = STDIN_FILENO;
    // batch mode
    if (argc == 2)
    {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0)
        {
            perror("open");
            free(buf);
            exit(EXIT_FAILURE);
        }
    }
    // interactive mode
    int is_interactive = (argc == 1 && isatty(fd));
    if (is_interactive)
    {
        printf("Welcome to my shell!\n");
        last_status = prepend_dir();
    }

    // read() each character of a given input one at a time until '\n', '\0', or error
    char c;
    while (1)
    {
        bytes = read(fd, &c, 1);

        if (bytes == -1) // error handling
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("read");
            free(buf);

            if (argc == 2)
            {
                close(fd);
            }

            return EXIT_FAILURE;
        }
        if (bytes == 0) // end of input
        {
            break;
        }
        if (c == '\n') // Replace new line with string terminator and process command
        {
            buf[buf_len] = '\0';
            last_status = process_command(buf, &should_exit);
            buf_len = 0;
            if (should_exit)
            {
                break;
            }
            if (argc == 1 && isatty(fd))
            {
                prepend_dir();
            }

            continue;
        }
        else // just process the line
        {
            if (buf_len >= buf_capacity - 1) // not enough space in buffer
            {
                if (buf_capacity > SIZE_MAX / 2)
                {
                    char msg[] = "Increasing memory will cause wrap overflow.\n";
                    write(STDERR_FILENO, msg, sizeof(msg) - 1);
                    free(buf);
                    exit(EXIT_FAILURE);
                }
                size_t new_buf_capacity = 2 * buf_capacity;

                char *temp = realloc(buf, new_buf_capacity);
                if (temp == NULL)
                {
                    perror("buf realloc");
                    free(buf);
                    exit(EXIT_FAILURE);
                }
                buf = temp;
                buf_capacity = new_buf_capacity;
            }

            buf[buf_len] = c; // write to buffer
            buf_len++;
        }
    }

    if (bytes == 0 && buf_len > 0) // EOF
    {
        buf[buf_len] = '\0';
        last_status = process_command(buf, &should_exit);
        buf_len = 0;
    }

    // Clean Up
    free(buf);
    if (argc == 2)
    {
        close(fd);
    }

    // interactive "good bye"
    if (is_interactive)
    {
        if (should_exit)
        {
            char msg[] = "Goodbye from my shell";
            write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        }
        else
        {
            char msg[] = "\nGoodbye from my shell";
            write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        }
    }
    return last_status;
}

int prepend_dir(void)
{
    char *cwd = get_current_dir();

    if (cwd == NULL)
    {
        perror("pwd");
        return EXIT_FAILURE;
    }

    const char *home = getenv("HOME");
    const char *cwd_path = cwd;

    if (home != NULL && home[0] != '\0')
    {
        size_t home_len = strlen(home);

        if (strncmp(cwd, home, home_len) == 0 && (cwd[home_len] == '/' || cwd[home_len] == '\0'))
        {
            write(STDOUT_FILENO, "~", 1);
            cwd_path = cwd + home_len;
        }
    }
    write(STDOUT_FILENO, cwd_path, strlen(cwd_path));
    write(STDOUT_FILENO, "$ ", 2);

    free(cwd);

    return EXIT_SUCCESS;
}

int process_command(char *command, int *should_exit)
{
    int exit_status = 0;
    int expect_input = 0;
    int expect_output = 0;
    size_t command_len = strlen(command);
    Command cmd = {0};
    size_t args_capacity = 10;
    cmd.args = malloc(args_capacity * sizeof(*cmd.args));
    if (cmd.args == NULL)
    {
        perror("malloc");
        return EXIT_FAILURE;
    }

    size_t i = 0;

    while (i < command_len) // tokenization
    {
        // Special Cases
        // Skip whitespace in the beginning
        while (command[i] != '\0' && isspace(command[i]))
        {
            i++;
        }
        // '\0' string terminator
        if (command[i] == '\0')
        {
            break;
        }
        // # comment terminator
        if (command[i] == '#')
        {
            break;
        }

        char *start = command + i;
        while (command[i] != '\0' && !isspace(command[i]) && command[i] != '#')
        {
            i++;
        }
        if (isspace(command[i]))
        {
            command[i] = '\0';
            i++;
        }

        if (cmd.argc >= args_capacity - 1)
        {
            if (args_capacity > SIZE_MAX / 2)
            {
                char msg[] = "Memory overflow in realloc()";
                write(STDERR_FILENO, msg, sizeof(msg) - 1);
                free(cmd.args);
                exit(EXIT_FAILURE);
            }

            size_t new_args_capacity = args_capacity * 2;
            if (new_args_capacity > SIZE_MAX / sizeof(*cmd.args))
            {
                char msg[] = "Memory overflow in realloc()";
                write(STDERR_FILENO, msg, sizeof(msg) - 1);
                free(cmd.args);
                exit(EXIT_FAILURE);
            }

            char **temp = realloc(cmd.args, new_args_capacity * sizeof(*cmd.args));
            if (temp == NULL)
            {
                perror("realloc()");
                free(cmd.args);
                exit(EXIT_FAILURE);
            }
            cmd.args = temp;
            args_capacity = new_args_capacity;
        }
        int is_redirect_operator = strcmp(start, ">") == 0 || strcmp(start, ">>") == 0 || strcmp(start, "<") == 0;
        if ((expect_input || expect_output) && is_redirect_operator)
        {
            fprintf(stderr, "Expected filename before operator\n");
            exit_status = EXIT_FAILURE;
            break;
        }
        if (expect_output)
        {
            cmd.output_file = start;
            expect_output = 0;
        }
        else if (strcmp(start, ">") == 0)
        {
            expect_output = 1;
            cmd.append = 0;
        }
        else if (strcmp(start, ">>") == 0)
        {
            expect_output = 1;
            cmd.append = 1;
        }
        else if (expect_input)
        {
            cmd.input_file = start;
            expect_input = 0;
        }
        else if (strcmp(start, "<") == 0)
        {
            expect_input = 1;
        }
        else
        {
            cmd.args[cmd.argc] = start;
            cmd.argc++;
        }

        if (command[i] == '#')
        {
            command[i] = '\0';
            break;
        }
    }

    cmd.args[cmd.argc] = NULL; // NULL for execv()

    if (exit_status == EXIT_SUCCESS && (expect_input || expect_output))
    {
        fprintf(stderr, "Missing redirection filename\n");
        exit_status = EXIT_FAILURE;
    }

    if (exit_status == EXIT_SUCCESS && cmd.argc > 0)
    {
        if (strcmp(cmd.args[0], "cd") == 0) // cd
        {
            if (cmd.argc > 2)
            {
                char msg[] = "cd: Too many arguments\n";
                write(STDERR_FILENO, msg, sizeof(msg) - 1);
                exit_status = EXIT_FAILURE;
            }
            else if (cmd.argc == 1)
            {
                char *home = getenv("HOME");
                if (home == NULL)
                {
                    char msg[] = "cd: No HOME environment set\n";
                    write(STDERR_FILENO, msg, sizeof(msg) - 1);
                    exit_status = EXIT_FAILURE;
                }
                else if (chdir(home) == -1)
                {
                    perror("cd");
                    exit_status = EXIT_FAILURE;
                }
            }
            else
            {
                if (chdir(cmd.args[1]) == -1)
                {
                    perror("cd");
                    exit_status = EXIT_FAILURE;
                }
            }
        }
        else if (strcmp(cmd.args[0], "pwd") == 0) // pwd
        {
            char *cwd = get_current_dir();
            if (cwd == NULL)
            {
                perror("pwd");
                exit_status = EXIT_FAILURE;
            }
            else
            {
                write(STDOUT_FILENO, cwd, strlen(cwd));
                write(STDOUT_FILENO, "\n", 1);
                free(cwd);
            }
        }
        else if (strcmp(cmd.args[0], "which") == 0) // which
        {
            if (cmd.argc > 2)
            {
                fprintf(stderr, "which: too many arguments\n");
                exit_status = EXIT_FAILURE;
            }
            else if (cmd.argc < 2)
            {
                fprintf(stderr, "which: missing arguments\n");
                exit_status = EXIT_FAILURE;
            }
            else
            {
                char *which_arg_path = executable_search(cmd.args[1]);
                if (which_arg_path != NULL)
                {
                    write(STDOUT_FILENO, which_arg_path, strlen(which_arg_path));
                    write(STDOUT_FILENO, "\n", 1);
                    free(which_arg_path);
                }
                else
                {
                    fprintf(stderr, "which: executable not found: %s\n", cmd.args[1]);
                    exit_status = EXIT_FAILURE;
                }
            }
        }
        else if (strcmp(cmd.args[0], "exit") == 0) // exit
        {
            if (cmd.argc > 1)
            {
                fprintf(stderr, "exit: Too many arguments.\n");
                exit_status = EXIT_FAILURE;
            }
            else
            {
                *should_exit = 1;
            }
        }
        else
        {
            char *executable = executable_search(cmd.args[0]);
            if (executable == NULL)
            {
                fprintf(stderr, "mysh: command not found: %s\n", cmd.args[0]);
                exit_status = EXIT_FAILURE;
            }
            else
            {

                pid_t c = fork();
                if (c == 0) // child
                {
                    int fd;
                    if (cmd.output_file != NULL)
                    {
                        int flags;
                        if (cmd.append)
                        {
                            flags = O_WRONLY | O_CREAT | O_APPEND;
                        }
                        else
                        {
                            flags = O_WRONLY | O_CREAT | O_TRUNC;
                        }
                        fd = open(cmd.output_file, flags, 0600);

                        if (fd < 0)
                        {
                            perror("open");
                            _exit(EXIT_FAILURE);
                        }
                        if (dup2(fd, STDOUT_FILENO) == -1)
                        {
                            perror("dup2");
                            close(fd);
                            _exit(EXIT_FAILURE);
                        }
                        close(fd);
                    }
                    if (cmd.input_file != NULL)
                    {
                        int fd = open(cmd.input_file, O_RDONLY);
                        if (fd < 0)
                        {
                            perror("open");
                            _exit(EXIT_FAILURE);
                        }
                        if (dup2(fd, STDIN_FILENO) == -1)
                        {
                            perror("dup2");
                            close(fd);
                            _exit(EXIT_FAILURE);
                        }
                        close(fd);
                    }

                    execv(executable, cmd.args);
                    perror("execv");
                    // _exit to prevent flushing data waiting in parent memory
                    _exit(EXIT_FAILURE);
                }
                else if (c > 0) // parent
                {
                    int status;
                    pid_t result = waitpid(c, &status, 0);

                    // Retry if signal occurred while system call wass in progress
                    while (result == -1 && errno == EINTR)
                    {
                        result = waitpid(c, &status, 0);
                    }

                    if (result == -1) // error
                    {
                        perror("waitpid");
                        exit_status = EXIT_FAILURE;
                    }
                    else if (WIFEXITED(status)) // Exit Status
                    {
                        exit_status = WEXITSTATUS(status);
                    }
                    else if (WIFSIGNALED(status)) // Terminated by a Signal
                    {
                        exit_status = WTERMSIG(status);
                    }
                }
                else // error
                {
                    perror("fork");
                    exit_status = EXIT_FAILURE;
                }
            }
            free(executable);
        }
    }

    free(cmd.args);
    return exit_status;
}

char *get_current_dir(void)
{
    size_t cwd_capacity = BUFSIZ;
    char *cwd = malloc(cwd_capacity);
    if (cwd == NULL)
    {
        return NULL;
    }
    while (getcwd(cwd, cwd_capacity) == NULL)
    {
        if (errno != ERANGE)
        {
            free(cwd);
            return NULL;
        }
        if (cwd_capacity > SIZE_MAX / 2)
        {
            free(cwd);
            errno = ENOMEM;
            return NULL;
        }
        size_t new_cwd_capacity = cwd_capacity * 2;

        char *temp = realloc(cwd, new_cwd_capacity);
        if (temp == NULL)
        {
            free(cwd);
            return NULL;
        }
        cwd = temp;
        cwd_capacity = new_cwd_capacity;
    }
    return cwd;
}

char *executable_search(const char *exe)
{
    const char *directories[] = {
        "/usr/local/bin",
        "/usr/bin",
        "/bin"};

    if (strchr(exe, '/') != NULL)
    {
        if (access(exe, X_OK) == 0)
        {
            size_t exe_size = strlen(exe) + 1;
            char *path = malloc(exe_size);

            if (path == NULL)
            {
                return NULL;
            }

            memcpy(path, exe, exe_size);
            return path;
        }
    }
    else
    {
        size_t directories_len = sizeof(directories) / sizeof(directories[0]);
        for (size_t i = 0; i < directories_len; i++)
        {
            size_t dir_len = strlen(directories[i]);
            size_t exe_len = strlen(exe);

            if (exe_len > SIZE_MAX - 2 ||
                dir_len > SIZE_MAX - exe_len - 2)
            {
                errno = ENOMEM;
                return NULL;
            }
            size_t path_size = dir_len + exe_len + 2;

            char *path = malloc(path_size);

            if (path == NULL)
            {
                return NULL;
            }
            // snprintf(memory where combine goes, size of memory, "str1 to strN to combine", str1 to strN to combine)
            // returns combined string length
            int combined_path_len = snprintf(path, path_size, "%s/%s", directories[i], exe);
            if (combined_path_len < 0 || (size_t)combined_path_len >= path_size)
            {
                free(path);
                continue;
            }
            if (access(path, X_OK) == 0)
            {
                return path;
            }

            free(path);
        }
        return NULL;
    }
    return NULL;
}
