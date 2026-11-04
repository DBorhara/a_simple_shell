#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

void print_curr_dir(void);
int process_command(char *command);

typedef struct
{
    char **args;
    size_t argc;
    char *input_file;
    char *output_file;
    int append;
} Command;

int main(int argc, char const *argv[])
{
    if (argc > 2)
    {
        char msg[] = "No more than 1 argument allowed.\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        exit(EXIT_FAILURE);
    }

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
        print_curr_dir();
    }
    char c;
    while ((bytes = read(fd, &c, 1)) > 0)
    {
        if (c == '\n')
        {
            buf[buf_len] = '\0';
            last_status = process_command(buf);
            buf_len = 0;
            if (argc == 1 && isatty(fd))
            {
                print_curr_dir();
            }

            continue;
        }
        else
        {
            if (buf_len >= buf_capacity - 1)
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

            buf[buf_len] = c;
            buf_len++;
        }
    }
    if (bytes == -1)
    {
        perror("read");
        exit(EXIT_FAILURE);
    }
    if (bytes == 0 && buf_len > 0)
    {
        last_status = process_command(buf);
        buf_len = 0;
    }
    free(buf);
    if (argc == 2)
    {
        close(fd);
    }
    if (is_interactive)
    {
        char msg[] = "\nGoodbye from my shell";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
    return last_status;
}

void print_curr_dir(void)
{
    size_t cwd_capacity = BUFSIZ;
    char *cwd = malloc(cwd_capacity);
    if (cwd == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    while (getcwd(cwd, cwd_capacity) == NULL)
    {
        if (errno != ERANGE)
        {
            perror("getcwd");
            free(cwd);
            exit(EXIT_FAILURE);
        }
        if (cwd_capacity > SIZE_MAX / 2)
        {
            free(cwd);
            char msg[] = "Memory overflow in realloc()";
            write(STDERR_FILENO, msg, sizeof(msg) - 1);
            free(cwd);
            exit(EXIT_FAILURE);
        }
        size_t new_cwd_capacity = cwd_capacity * 2;

        char *temp = realloc(cwd, new_cwd_capacity);
        if (temp == NULL)
        {
            perror("cwd realloc");
            free(cwd);
            exit(EXIT_FAILURE);
        }
        cwd = temp;
        cwd_capacity = new_cwd_capacity;
    }

    const char *home = getenv("HOME");
    if (home == NULL)
    {
        char msg[] = "Home directory is NULL";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        free(cwd);
        exit(EXIT_FAILURE);
    }

    size_t home_len = strlen(home);

    // Inside HOME directory
    if (strncmp(cwd, home, home_len) == 0 && (cwd[home_len] == '/' || cwd[home_len] == '\0'))
    {
        char *sub_dir_path = cwd + home_len;

        write(STDOUT_FILENO, "~", 1);
        write(STDOUT_FILENO, sub_dir_path, strlen(sub_dir_path));
        write(STDOUT_FILENO, "$ ", 2);
    }
    else // Outside HOME directory
    {
        write(STDOUT_FILENO, cwd, strlen(cwd));
        write(STDOUT_FILENO, "$ ", 2);
    }
    free(cwd);
}

int process_command(char *command)
{
    int exit_status = 0;
    size_t command_len = strlen(command);
    size_t tokens_capacity = 10;
    char **tokens = malloc(tokens_capacity * sizeof(*tokens));
    if (tokens == NULL)
    {
        perror("tokens");
        exit(EXIT_FAILURE);
    }
    size_t tokens_len = 0;
    size_t i = 0;

    while (i < command_len)
    {
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

        if (tokens_len >= tokens_capacity - 1)
        {
            if (tokens_capacity > SIZE_MAX / 2)
            {
                char msg[] = "Memory overflow in tokens realloc()";
                write(STDERR_FILENO, msg, sizeof(msg) - 1);
                free(tokens);
                exit(EXIT_FAILURE);
            }
            size_t new_tokens_capacity = tokens_capacity * 2;
            if (new_tokens_capacity > SIZE_MAX / sizeof(*tokens))
            {
                char msg[] = "Memory overflow in tokens realloc()";
                write(STDERR_FILENO, msg, sizeof(msg) - 1);
                free(tokens);
                exit(EXIT_FAILURE);
            }

            char **temp = realloc(tokens, new_tokens_capacity * sizeof(*tokens));
            if (temp == NULL)
            {
                perror("tokens realloc");
                free(tokens);
                exit(EXIT_FAILURE);
            }
            tokens = temp;
            tokens_capacity = new_tokens_capacity;
        }
        tokens[tokens_len] = start;
        tokens_len++;
        if (command[i] == '#')
        {
            command[i] = '\0';
            break;
        }
    }
    tokens[tokens_len] = NULL;

    if (tokens_len > 0)
    {
        const char *directories[] = {
            "/usr/local/bin",
            "/usr/bin",
            "/bin"};

        char path[BUFSIZ];
        char *exectuable = NULL;

        if (strchr(tokens[0], '/') != NULL)
        {
            if (access(tokens[0], X_OK) == 0)
            {
                exectuable = tokens[0];
            }
        }
        else
        {
            int directories_len = sizeof(directories) / sizeof(directories[0]);
            for (size_t i = 0; i < directories_len; i++)
            {
                int exe = snprintf(path, sizeof(path), "%s/%s", directories[i], tokens[0]);

                // Too big for buffer
                if (exe < 0 || (size_t)exe >= sizeof(path))
                {
                    continue;
                }
                // Found our first executable
                if (access(path, X_OK) == 0)
                {
                    exectuable = path;
                    break;
                }
            }
        }
        if (exectuable != NULL)
        {
            pid_t c = fork();
            if (c == 0) // child
            {
                execv(exectuable, tokens);
                perror("execv");
                // _exit to prevent flushing data waiting in parent memory
                _exit(EXIT_FAILURE);
            }
            else if (c > 0) // parent
            {
                int status;
                if (waitpid(c, &status, 0) == -1) // error
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
    }

    free(tokens);
    return exit_status;
}
