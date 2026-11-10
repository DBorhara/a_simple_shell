#define _POSIX_C_SOURCE 200809L

#include "mysh.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <glob.h>
#include <signal.h>

int main(int argc, char const *argv[])
{
  if (argc > 2)
  {
    char msg[] = "No more than 1 argument allowed.\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    exit(EXIT_FAILURE);
  }
  int should_exit = 0;
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
  int is_interactive = (argc == 1 && isatty(fd));
  int batch_mode = !is_interactive;
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
  if (is_interactive)
  {
    printf("Welcome to my shell!\n");
    prepend_dir();
  }

  // read() each character of a given input one at a time until '\n', '\0', or
  // error
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
      int signal_number = 0;

      int command_status = process_command(buf, &should_exit, batch_mode, &signal_number);

      buf_len = 0;

      if (is_interactive)
      {
        report_command_status(command_status, signal_number);
      }
      if (should_exit)
      {
        break;
      }
      if (is_interactive)
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
    int signal_number = 0;
    int command_status = process_command(buf, &should_exit, batch_mode, &signal_number);

    if (is_interactive)
    {
      report_command_status(command_status, signal_number);
    }

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
      char msg[] = "Goodbye from my shell\n";
      write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
    else
    {
      char msg[] = "\nGoodbye from my shell\n";
      write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }
  }
  return EXIT_SUCCESS;
}

void report_command_status(int status, int signal_number)
{
  if (signal_number != 0)
  {
    fprintf(stderr, "Terminated by signal number: %d (%s)\n", signal_number, strsignal(signal_number));
  }
  else if (status != EXIT_SUCCESS)
  {
    fprintf(stderr, "Exited with status: %d\n", status);
  }
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

    if (strncmp(cwd, home, home_len) == 0 &&
        (cwd[home_len] == '/' || cwd[home_len] == '\0'))
    {
      write(STDOUT_FILENO, "~", 1);
      cwd_path = cwd + home_len;
    }
  }
  write(STDOUT_FILENO, cwd_path, strlen(cwd_path));
  write(STDOUT_FILENO, "$ ", 2);

  free(cwd);

  return EXIT_SUCCESS;
} // }}}

int process_command(char *command, int *should_exit, int batch_mode, int *signal_number)
{
  *signal_number = 0;
  int exit_status = 0;
  int expect_input = 0;
  int expect_output = 0;
  int has_pipe = 0;
  glob_t glob_results = {0};
  int has_glob_results = 0;
  size_t command_len = strlen(command);

  size_t commands_capacity = 2;
  size_t commands_count = 1;

  Command *commands = calloc(commands_capacity, sizeof(*commands));

  if (commands == NULL)
  {
    perror("calloc");
    return EXIT_FAILURE;
  }

  Command *current = &commands[0];

  size_t args_capacity = 10;
  current->args = malloc(args_capacity * sizeof(*current->args));
  if (current->args == NULL)
  {
    perror("malloc");
    free(commands);
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

    if (current->argc >= args_capacity - 1)
    {
      if (args_capacity > SIZE_MAX / 2)
      {
        char msg[] = "Memory overflow in realloc()";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        free(current->args);
        exit(EXIT_FAILURE);
      }

      size_t new_args_capacity = args_capacity * 2;
      if (new_args_capacity > SIZE_MAX / sizeof(*current->args))
      {
        char msg[] = "Memory overflow in realloc()";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        free(current->args);
        exit(EXIT_FAILURE);
      }

      char **temp =
          realloc(current->args, new_args_capacity * sizeof(*current->args));
      if (temp == NULL)
      {
        perror("realloc()");
        free(current->args);
        exit(EXIT_FAILURE);
      }
      current->args = temp;
      args_capacity = new_args_capacity;
    }
    int is_operator = strcmp(start, ">") == 0 ||
                      strcmp(start, ">>") == 0 ||
                      strcmp(start, "<") == 0 ||
                      strcmp(start, "|") == 0;
    if ((expect_input || expect_output) && is_operator)
    {
      fprintf(stderr, "Expected filename before operator\n");
      exit_status = EXIT_FAILURE;
      break;
    }
    if (((strcmp(start, ">") == 0) || (strcmp(start, ">>") == 0)) && current->output_file != NULL)
    {
      fprintf(stderr, "Duplicate output redirection\n");
      exit_status = EXIT_FAILURE;
      break;
    }
    if (strcmp(start, "<") == 0 && current->input_file != NULL)
    {
      fprintf(stderr, "Duplicate input redirection\n");
      exit_status = EXIT_FAILURE;
      break;
    }

    if (expect_output)
    {
      current->output_file = start;
      expect_output = 0;
    }
    else if (strcmp(start, ">") == 0)
    {
      expect_output = 1;
      current->append = 0;
    }
    else if (strcmp(start, ">>") == 0)
    {
      expect_output = 1;
      current->append = 1;
    }
    else if (strcmp(start, "|") == 0)
    {
      if (current->argc == 0 || expect_input || expect_output)
      {
        fprintf(stderr, "Invalid pipe\n");
        exit_status = EXIT_FAILURE;
        break;
      }

      current->args[current->argc] = NULL;
      if (commands_capacity <= commands_count)
      {
        if (commands_capacity > SIZE_MAX / 2 || commands_capacity * 2 > SIZE_MAX / sizeof(*commands))
        {
          fprintf(stderr, "Command array is too large\n");
          exit_status = EXIT_FAILURE;
          break;
        }

        size_t new_capacity = commands_capacity * 2;
        Command *temp = realloc(commands, new_capacity * sizeof(*commands));
        if (temp == NULL)
        {
          perror("realloc");
          exit_status = EXIT_FAILURE;
          break;
        }
        commands = temp;
        commands_capacity = new_capacity;
        current = &commands[commands_count - 1];
      }

      commands[commands_count] = (Command){0};
      size_t new_args_capcity = 10;
      commands[commands_count].args = malloc(new_args_capcity * sizeof(*commands[commands_count].args));
      if (commands[commands_count].args == NULL)
      {
        perror("malloc");
        exit_status = EXIT_FAILURE;
        break;
      }

      current = &commands[commands_count];
      commands_count++;
      args_capacity = new_args_capcity;
      has_pipe = 1;
    }
    else if (expect_input)
    {
      current->input_file = start;
      expect_input = 0;
    }
    else if (strcmp(start, "<") == 0)
    {
      expect_input = 1;
    }
    else if (strchr(start, '*') != NULL)
    {
      size_t glob_count = glob_results.gl_pathc;
      int flags = 0;
      if (has_glob_results)
      {
        flags = GLOB_APPEND;
      }

      int result = glob(start, flags, NULL, &glob_results);

      if (result == 0)
      {
        has_glob_results = 1;

        for (size_t j = glob_count; j < glob_results.gl_pathc; j++)
        {
          if (current->argc >= args_capacity - 1)
          {
            size_t new_args_capacity = args_capacity * 2;
            if (new_args_capacity > SIZE_MAX / sizeof(*current->args))
            {
              char msg[] = "Memory overflow in realloc()";
              write(STDERR_FILENO, msg, sizeof(msg) - 1);
              free(current->args);
              exit(EXIT_FAILURE);
            }
            char **temp = realloc(current->args, new_args_capacity * sizeof(*current->args));

            if (temp == NULL)
            {
              perror("realloc");
              exit_status = EXIT_FAILURE;
              break;
            }
            current->args = temp;
            args_capacity = new_args_capacity;
          }
          if (exit_status != EXIT_SUCCESS)
          {
            break;
          }

          current->args[current->argc] = glob_results.gl_pathv[j];
          current->argc++;
        }
        if (exit_status != EXIT_SUCCESS)
        {
          break;
        }
      }
      else if (result == GLOB_NOMATCH)
      {
        current->args[current->argc] = start;
        current->argc++;
      }
      else
      {
        fprintf(stderr, "mysh: glob failed\n");
        exit_status = EXIT_FAILURE;
      }
    }
    else
    {
      current->args[current->argc] = start;
      current->argc++;
    }

    if (command[i] == '#')
    {
      command[i] = '\0';
      break;
    }
  }

  current->args[current->argc] = NULL; // NULL for execv()

  if (exit_status == EXIT_SUCCESS && (expect_input || expect_output))
  {
    fprintf(stderr, "Missing redirection filename.\n");
    exit_status = EXIT_FAILURE;
  }

  if (exit_status == EXIT_SUCCESS && has_pipe && (commands[commands_count - 1].argc == 0))
  {
    fprintf(stderr, "Missing command after pipe.\n");
    exit_status = EXIT_FAILURE;
  }

  if (exit_status == EXIT_SUCCESS && has_pipe)
  {
    size_t pipe_count = commands_count - 1;

    if (pipe_count > SIZE_MAX / sizeof(int[2]))
    {
      fprintf(stderr, "Too many pipes\n");
      exit_status = EXIT_FAILURE;
    }
    else
    {
      int (*pipefds)[2] = malloc(pipe_count * sizeof(*pipefds));

      if (pipefds == NULL)
      {
        perror("malloc");
        exit_status = EXIT_FAILURE;
      }
      else
      {
        size_t created_pipes = 0;

        for (size_t i = 0; i < pipe_count; i++)
        {
          if (pipe(pipefds[i]) == -1)
          {
            perror("pipe");
            exit_status = EXIT_FAILURE;
            break;
          }
          created_pipes++;
        }

        pid_t *pids = NULL;
        size_t child_counter = 0;
        if (exit_status == EXIT_SUCCESS)
        {
          if (commands_count > SIZE_MAX / sizeof(*pids))
          {
            fprintf(stderr, "Too many processes\n");
            exit_status = EXIT_FAILURE;
          }
          else
          {
            pids = malloc(commands_count * sizeof(*pids));
            if (pids == NULL)
            {
              perror("malloc");
              exit_status = EXIT_FAILURE;
            }
            else
            {
              for (size_t i = 0; i < commands_count; i++) // fork loop for n children
              {
                pid_t pid = fork(); // Create children

                if (pid == -1)
                {
                  perror("fork");
                  exit_status = EXIT_FAILURE;
                  break;
                }
                else if (pid == 0) // Check if in children
                {
                  if (batch_mode && i == 0 && commands[i].input_file == NULL)
                  {
                    int dev_null = open("/dev/null", O_RDONLY);
                    if (dev_null == -1)
                    {
                      perror("open");
                      _exit(EXIT_FAILURE);
                    }
                    if (dup2(dev_null, STDIN_FILENO) == -1)
                    {
                      perror("dup2");
                      close(dev_null);
                      _exit(EXIT_FAILURE);
                    }
                    close(dev_null);
                  }

                  // Read from the previous pipe
                  if (i > 0)
                  {
                    if (dup2(pipefds[i - 1][0], STDIN_FILENO) == -1)
                    {
                      perror("dup2");
                      _exit(EXIT_FAILURE);
                    }
                  }
                  // Write to the next pipe
                  if (i < commands_count - 1)
                  {
                    if (dup2(pipefds[i][1], STDOUT_FILENO) == -1)
                    {
                      perror("dup2");
                      _exit(EXIT_FAILURE);
                    }
                  }
                  for (size_t j = 0; j < pipe_count; j++)
                  {
                    close(pipefds[j][0]);
                    close(pipefds[j][1]);
                  }

                  int builtin_status = execute_builtin(&commands[i], should_exit);

                  if (builtin_status != -1)
                  {
                    _exit(builtin_status);
                  }

                  char *path = executable_search(commands[i].args[0]);

                  if (path == NULL)
                  {
                    fprintf(stderr, "Command not found: %s\n", commands[i].args[0]);
                    _exit(EXIT_FAILURE);
                  }

                  execv(path, commands[i].args);

                  perror("execv");
                  free(path);
                  _exit(EXIT_FAILURE);
                }
                else // Check if in parent
                {
                  pids[child_counter] = pid;
                  child_counter++;
                }
              }
            }
          }
        }
        for (size_t i = 0; i < created_pipes; i++)
        {
          close(pipefds[i][0]);
          close(pipefds[i][1]);
        }

        int last_child_status = 0;
        int wait_failure = 0;
        for (size_t i = 0; i < child_counter; i++)
        {
          int status = 0;
          pid_t result;

          do
          {
            result = waitpid(pids[i], &status, 0);
          } while (result == -1 && errno == EINTR);

          if (result == -1)
          {
            perror("waitpid");
            wait_failure = 1;
          }
          else if (i == commands_count - 1)
          {
            last_child_status = status;
          }
        }
        int pipeline_complete = (exit_status == EXIT_SUCCESS &&
                                 child_counter == commands_count &&
                                 !wait_failure);

        if (wait_failure)
        {
          exit_status = EXIT_FAILURE;
        }
        else if (pipeline_complete)
        {
          if (WIFEXITED(last_child_status))
          {
            exit_status = WEXITSTATUS(last_child_status);
          }
          else if (WIFSIGNALED(last_child_status))
          {
            *signal_number = WTERMSIG(last_child_status);
            exit_status = 128 + *signal_number;
          }
          else
          {
            exit_status = EXIT_FAILURE;
          }
        }
        if (pipeline_complete)
        {
          for (size_t i = 0; i < commands_count; i++)
          {
            if (strcmp(commands[i].args[0], "exit") == 0)
            {
              *should_exit = 1;
              break;
            }
          }
        }

        free(pids);
        free(pipefds);
      }
    }
  }

  if (exit_status == EXIT_SUCCESS && !has_pipe && current->argc > 0)
  {
    int is_builtin =
        strcmp(current->args[0], "cd") == 0 ||
        strcmp(current->args[0], "pwd") == 0 ||
        strcmp(current->args[0], "which") == 0 ||
        strcmp(current->args[0], "exit") == 0;

    if (is_builtin)
    {
      int saved_stdout = -1;
      int saved_stdin = -1;
      int ready = 1;
      if (current->output_file != NULL && ready)
      {
        saved_stdout = dup(STDOUT_FILENO);

        if (saved_stdout == -1)
        {
          perror("dup");
          ready = 0;
          exit_status = EXIT_FAILURE;
        }

        if (ready)
        {
          int flags;
          if (current->append)
          {
            flags = O_WRONLY | O_CREAT | O_APPEND;
          }
          else
          {
            flags = O_WRONLY | O_CREAT | O_TRUNC;
          }
          int fd = open(current->output_file, flags, 0640);
          if (fd == -1)
          {
            perror("open");
            ready = 0;
            exit_status = EXIT_FAILURE;
          }
          else
          {
            if (dup2(fd, STDOUT_FILENO) == -1)
            {
              perror("dup2");
              ready = 0;
              exit_status = EXIT_FAILURE;
            }
            close(fd);
          }
        }
      }
      if (ready && current->input_file != NULL)
      {
        saved_stdin = dup(STDIN_FILENO);
        if (saved_stdin == -1)
        {
          perror("dup");
          ready = 0;
          exit_status = EXIT_FAILURE;
        }
        if (ready)
        {
          int fd = open(current->input_file, O_RDONLY);
          if (fd == -1)
          {
            perror("open");
            ready = 0;
            exit_status = EXIT_FAILURE;
          }
          else
          {
            if (dup2(fd, STDIN_FILENO) == -1)
            {
              perror("dup2");
              ready = 0;
              exit_status = EXIT_FAILURE;
            }
            close(fd);
          }
        }
      }

      if (ready)
      {
        exit_status = execute_builtin(current, should_exit);
      }

      if (saved_stdout != -1)
      {
        if (dup2(saved_stdout, STDOUT_FILENO) == -1)
        {
          perror("dup2");
          exit_status = EXIT_FAILURE;
        }
        close(saved_stdout);
      }
      if (saved_stdin != -1)
      {
        if (dup2(saved_stdin, STDIN_FILENO) == -1)
        {
          perror("dup2");
          exit_status = EXIT_FAILURE;
        }
        close(saved_stdin);
      }
    }
    else
    {
      char *executable = executable_search(current->args[0]);
      if (executable == NULL)
      {
        fprintf(stderr, "mysh: command not found: %s\n", current->args[0]);
        exit_status = EXIT_FAILURE;
      }
      else
      {

        pid_t c = fork();
        if (c == 0) // child
        {
          int fd;
          if (current->output_file != NULL)
          {
            int flags;
            if (current->append)
            {
              flags = O_WRONLY | O_CREAT | O_APPEND;
            }
            else
            {
              flags = O_WRONLY | O_CREAT | O_TRUNC;
            }
            fd = open(current->output_file, flags, 0640);

            if (fd == -1)
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
          if (current->input_file != NULL)
          {
            int fd = open(current->input_file, O_RDONLY);
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
          else if (batch_mode)
          {
            int dev_null = open("/dev/null", O_RDONLY);
            if (dev_null == -1)
            {
              perror("open");
              _exit(EXIT_FAILURE);
            }
            if (dup2(dev_null, STDIN_FILENO) == -1)
            {
              perror("dup2");
              close(dev_null);
              _exit(EXIT_FAILURE);
            }
            close(dev_null);
          }

          execv(executable, current->args);
          perror("execv");
          // _exit to prevent flushing data waiting in parent memory
          _exit(EXIT_FAILURE);
        }
        else if (c > 0) // parent
        {
          int status;
          pid_t result = waitpid(c, &status, 0);

          // Retry if signal occurred while system call was in progress
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
            *signal_number = WTERMSIG(status);
            exit_status = 128 + *signal_number;
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
  // Clean up
  globfree(&glob_results);
  for (size_t j = 0; j < commands_count; j++)
  {
    free(commands[j].args);
  }

  free(commands);

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
  const char *directories[] = {"/usr/local/bin", "/usr/bin", "/bin"};

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

      if (exe_len > SIZE_MAX - 2 || dir_len > SIZE_MAX - exe_len - 2)
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
      // snprintf(memory where combine goes, size of memory, "str1 to strN to
      // combine", str1 to strN to combine) returns combined string length
      int combined_path_len =
          snprintf(path, path_size, "%s/%s", directories[i], exe);
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

int execute_builtin(Command *cmd, int *should_exit)
{
  if (strcmp(cmd->args[0], "pwd") == 0)
  {
    char *cwd = get_current_dir();
    if (cwd == NULL)
    {
      perror("pwd");
      return EXIT_FAILURE;
    }
    else
    {
      write(STDOUT_FILENO, cwd, strlen(cwd));
      write(STDOUT_FILENO, "\n", 1);
      free(cwd);
      return EXIT_SUCCESS;
    }
  }

  if (strcmp(cmd->args[0], "cd") == 0)
  {
    if (cmd->argc > 2)
    {
      fprintf(stderr, "cd: Too many args\n");
      return EXIT_FAILURE;
    }
    const char *path = NULL;
    if (cmd->argc == 1)
    {
      path = getenv("HOME");
      if (path == NULL)
      {
        fprintf(stderr, "HOME env not set\n");
        return EXIT_FAILURE;
      }
    }
    else
    {
      path = cmd->args[1];
    }
    if (chdir(path) == -1)
    {
      perror("cd");
      return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
  }

  if (strcmp(cmd->args[0], "which") == 0)
  {
    if (cmd->argc < 2)
    {
      fprintf(stderr, "which: missing args\n");
      return EXIT_FAILURE;
    }
    int status = EXIT_SUCCESS;
    for (size_t i = 1; i < cmd->argc; i++)
    {
      char *path = executable_search(cmd->args[i]);
      if (path == NULL)
      {
        fprintf(stderr, "which: executable not found: %s\n", cmd->args[i]);
        status = EXIT_FAILURE;
        continue;
      }
      write(STDOUT_FILENO, path, strlen(path));
      write(STDOUT_FILENO, "\n", 1);
      free(path);
    }
    return status;
  }

  if (strcmp(cmd->args[0], "exit") == 0)
  {
    *should_exit = 1;
    return EXIT_SUCCESS;
  }

  return -1;
}
