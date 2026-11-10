CS214 Project 3: mysh - Unix Shell

--------------------------------------------------------------------------------------------------------

AUTHORS

Name: Depak Borhara  
NetID: db1532

--------------------------------------------------------------------------------------------------------

OVERVIEW

This project implements a Unix shell in C using POSIX system calls.

The shell reads commands, parses arguments, executes programs, and reports command results.

The program supports:

- Interactive and batch modes.
- Built-in commands: cd, pwd, which, and exit.
- External command execution using fork(), execv(), and waitpid().
- Input redirection with <.
- Output redirection with > and >>.
- Pipelines containing multiple commands.
- Wildcard expansion using *.
- Comments beginning with #.
- Dynamic memory allocation.
- Exit-status reporting and signal detection.
- Error handling that allows the shell to continue after failed commands.

--------------------------------------------------------------------------------------------------------

COMMAND READING

main() reads input one character at a time using read().

A newline marks the end of a command. The shell processes the command before reading the next line.

The input buffer grows using realloc() when necessary. This allows commands longer than the initial buffer capacity.

The shell also processes the final command when the input reaches end-of-file without a trailing newline.

Interactive mode:

- Prints a welcome message.
- Displays the current working directory as the prompt.
- Replaces the home-directory prefix with ~.
- Executes commands until exit or end-of-file.
- Reports unsuccessful commands before printing the next prompt.
- Prints a goodbye message when the shell terminates.

Batch mode:

- Reads commands from a file or standard input.
- Does not print interactive prompts or welcome messages.
- Continues processing after command failures.

--------------------------------------------------------------------------------------------------------

COMMAND PARSING

process_command() separates each input line into tokens.

Whitespace separates normal arguments.

The parser recognizes these operators:

<    Input redirection  
>    Output redirection  
>>   Append output redirection  
|    Pipeline separator

The # character begins a comment. The shell ignores the rest of that line.

Each Command structure stores:

- The argument array.
- The argument count.
- The input filename.
- The output filename.
- The output append flag.

The shell stores Command structures in a dynamic array.

Each command also contains a dynamically allocated argument array. The arrays grow using realloc() when their capacity is reached.

The parser checks for syntax errors, including missing filenames, duplicate redirection operators, and missing commands around pipe operators.

An invalid command reports an error and does not stop the shell.

--------------------------------------------------------------------------------------------------------

EXECUTABLE SEARCH

executable_search() locates executable programs.

The shell searches these directories in order:

1. /usr/local/bin
2. /usr/bin
3. /bin

A command containing / is treated as an explicit executable path.

The search function uses access() to check whether a file is executable.

If an executable is found, the shell starts a child process and uses execv() to execute it.

The parent uses waitpid() to wait for the child and obtain its termination status.

--------------------------------------------------------------------------------------------------------

BUILT-IN COMMANDS

execute_builtin() implements cd, pwd, which, and exit.

cd:

Changes the current working directory using chdir().

Without an argument, cd changes to the home directory. With one argument, it changes to the specified directory.

pwd:

Prints the current working directory using getcwd().

which:

Accepts one or more executable names.

It prints the path of each executable that it finds. It reports failure when an executable cannot be found or when no arguments are supplied.

exit:

Ignores its arguments and requests termination of the shell.

Standalone built-in commands run in the parent process.

Built-in commands in pipelines run in child processes.

An exit command inside a pipeline causes the parent shell to terminate after the pipeline completes.

--------------------------------------------------------------------------------------------------------

WILDCARD EXPANSION

The shell uses glob() to expand arguments containing *.

The wildcard represents zero or more characters.

Supported patterns include:

*.c  
main*  
main*test.c  
src/*.c

Multiple wildcard arguments can appear in one command.

Matching pathnames replace the original wildcard argument.

When a pattern has no matches, the shell preserves the original argument.

Patterns beginning with * do not match hidden filenames.

The implementation uses globfree() to release memory allocated during wildcard expansion.

The argument array grows as necessary when a pattern matches multiple files.

--------------------------------------------------------------------------------------------------------

INPUT AND OUTPUT REDIRECTION

The shell supports three redirection operators.

< reads input from a specified file.

> writes output to a specified file and replaces existing content.

>> appends output to a specified file.

The implementation uses open(), dup2(), and close() to configure file descriptors.

Output files are created with permission mode 0640, subject to the process umask.

For external commands, redirection occurs in the child process.

For standalone built-in commands, the parent saves the original file descriptors before applying redirection.

After the built-in command completes, the shell restores the original descriptors.

If a file cannot be opened, the command fails and the shell reports an error.

--------------------------------------------------------------------------------------------------------

PIPELINES

The shell supports pipelines containing multiple commands.

For N commands, the shell creates N - 1 pipes.

Each pipeline command runs in a separate child process.

The first child writes to the first pipe.

Middle children read from the previous pipe and write to the next pipe.

The final child reads from the previous pipe.

The implementation uses pipe() to create pipes and dup2() to connect standard input and output.

Each child closes the original pipe descriptors after setting up its input and output.

The parent closes its pipe descriptors after creating the children.

The parent then uses waitpid() to wait for every successfully created child.

The result of a completed pipeline is determined by its final command.

Example:

echo hello | cat | wc -c

Expected output:

6

--------------------------------------------------------------------------------------------------------

BATCH MODE

Batch mode accepts commands from a specified file or redirected standard input.

Commands without an explicit input source receive /dev/null as standard input.

For pipelines, this rule applies to the first child. Later children read from their preceding pipe.

This prevents a child process from reading commands that belong to the shell's batch input.

Example batch file:

echo before  
cat  
echo after  
cat | wc -l

Expected output:

before  
after  
0

Batch mode does not print interactive prompts or command-status messages.

--------------------------------------------------------------------------------------------------------

EXIT STATUS AND SIGNAL HANDLING

The shell uses waitpid() to obtain child process termination information.

WIFEXITED() checks whether a child exited normally.

WEXITSTATUS() retrieves the normal exit code.

WIFSIGNALED() checks whether a child terminated because of a signal.

WTERMSIG() retrieves the terminating signal number.

The shell retries waitpid() when the call is interrupted by EINTR.

For signal termination, the implementation stores the signal number separately from the exit status.

The exit status uses the convention 128 + signal number.

In interactive mode:

- Successful commands do not print a status message.
- Failed commands print their exit status.
- Signal-terminated commands print the signal number and its description.

Examples:

Exited with status 1  
Terminated by signal 15 (Terminated)

The shell continues accepting commands after normal command failures and child signal termination.

Normal shell termination returns EXIT_SUCCESS, regardless of the previous command's exit status.

--------------------------------------------------------------------------------------------------------

MEMORY MANAGEMENT

The implementation uses dynamic memory allocation for input and command storage.

- main() allocates and expands the input buffer.
- process_command() allocates the command array using calloc().
- Each Command contains a dynamically allocated argument array.
- realloc() increases array capacity when necessary.
- executable_search() allocates memory for executable pathnames.
- glob() allocates memory for wildcard results.
- globfree() releases wildcard memory.
- Pipeline execution allocates pipe-descriptor and child-PID arrays.
- The parent closes pipe descriptors and waits for created children.
- process_command() releases command argument arrays and the command array before returning.

Memory allocation failures are checked and reported.

Array growth includes overflow checks.

--------------------------------------------------------------------------------------------------------

TESTING STRATEGY

The tests check command parsing, external execution, built-in commands, redirection, pipelines, wildcard expansion, batch input, error recovery, and memory safety.

TEST 1: Basic external command

Command: echo hello

Expected: Prints hello.

Purpose: Verify fork(), execv(), and waitpid() execution.

TEST 2: Working directory commands

Commands: pwd, cd /tmp, pwd

Expected: The working directory changes to /tmp.

Purpose: Verify pwd and cd behavior.

TEST 3: Executable search

Command: which ls cat grep

Expected: Prints executable paths for the specified programs.

Purpose: Verify executable search and multiple arguments.

TEST 4: Output redirection

Commands: pwd > directory.txt, cat directory.txt

Expected: The file contains the current working directory.

Purpose: Verify output redirection and file creation.

TEST 5: Append redirection

Command: pwd >> directory.txt

Expected: Adds another line without removing existing file content.

Purpose: Verify O_APPEND behavior.

TEST 6: Input redirection

Command: cat < input.txt

Expected: Prints the contents of input.txt.

Purpose: Verify input redirection.

TEST 7: Basic pipeline

Command: echo hello | cat

Expected: Prints hello.

Purpose: Verify communication between two child processes.

TEST 8: Multiple pipelines

Command: echo hello | cat | cat | wc -c

Expected: Prints 6.

Purpose: Verify multiple pipes and child processes.

TEST 9: Built-in commands in pipelines

Command: pwd | cat

Expected: Prints the current working directory.

Purpose: Verify execution of built-in commands in child processes.

TEST 10: Pipeline exit status

Commands: true | false, false | true

Expected: The first pipeline reports failure in interactive mode. The second succeeds without a failure-status message.

Purpose: Verify that the final pipeline command determines the result.

TEST 11: Exit inside a pipeline

Command: exit | false

Expected: The pipeline completes and the shell terminates.

Purpose: Verify exit handling across child and parent processes.

TEST 12: Wildcard expansion

Commands: echo *.c, echo *.h, echo src/*.c

Expected: Matching pathnames replace wildcard arguments.

Purpose: Verify wildcard expansion and directory prefixes.

TEST 13: Unmatched wildcard

Command: echo *.nonexistent

Expected: Prints *.nonexistent.

Purpose: Verify that unmatched patterns remain unchanged.

TEST 14: Hidden files

Place visible and hidden .c files in one directory. Run echo *.c.

Expected: Hidden filenames are not included.

Purpose: Verify hidden-file matching rules.

TEST 15: Invalid pipeline syntax

Commands:

echo hello |  
| cat  
echo hello | | cat  
echo hello | cat |

Expected: Each command reports a syntax error. The shell continues processing later commands.

Purpose: Verify parser validation and error recovery.

TEST 16: Dynamic argument allocation

Command: echo a b c d e f g h i j k l

Expected: Prints every argument in order.

Purpose: Verify argument-array growth.

TEST 17: Dynamic pipeline allocation

Command: echo hello | cat | cat | cat | cat | wc -c

Expected: Prints 6.

Purpose: Verify command-array growth and multiple pipe creation.

TEST 18: Batch-mode input

Run the same batch file using:

./mysh batch.txt  
./mysh < batch.txt

Expected: Both methods execute the same commands without interactive prompts.

Purpose: Verify file-based and standard-input batch modes.

TEST 19: Batch input isolation

Place cat between two echo commands in a batch file.

Expected: cat receives end-of-file from /dev/null. The later echo command still executes.

Purpose: Verify that child processes do not consume batch commands.

TEST 20: Failed command recovery

Commands: nonexistentcommand, echo still-working

Expected: The shell reports the missing command and then prints still-working.

Purpose: Verify error recovery.

TEST 21: Normal exit-status reporting

Commands: true, false

Expected: true prints no failure message. false reports exit status 1 in interactive mode.

Purpose: Verify WIFEXITED() and WEXITSTATUS().

TEST 22: Signal termination

Run sleep 30 in the shell. From another terminal, send SIGTERM to that child process.

Expected: The shell reports signal 15 and continues accepting commands.

Purpose: Verify WIFSIGNALED(), WTERMSIG(), and signal reporting.

TEST 23: End-of-file handling

Use a batch file whose final command has no trailing newline.

Expected: The final command executes once.

Purpose: Verify input handling at end-of-file.

TEST 24: Memory safety

Build with AddressSanitizer and UndefinedBehaviorSanitizer.

Run the earlier tests and check for sanitizer reports.

Expected: No invalid memory access or undefined behavior is reported.

Purpose: Verify allocation, pointer use, and resource cleanup.

--------------------------------------------------------------------------------------------------------

KNOWN LIMITATIONS

- The shell does not implement quoted strings or escape sequences.
- The shell does not implement environment-variable expansion or command substitution.
- The parser assumes redirection and pipeline operators are separated from normal arguments by whitespace.
- Wildcard support is intended for patterns with one asterisk in the final pathname component.
- Redirection within pipeline subcommands is not implemented. The assignment permits this assumption.
- The shell searches only the three required executable directories and explicit paths.

--------------------------------------------------------------------------------------------------------

BUILDING

Run: make

Builds the mysh executable from mysh.c and mysh.h using the provided Makefile.

The original Makefile uses C99, compiler warnings, AddressSanitizer, and UndefinedBehaviorSanitizer.

Run: make clean

Removes the generated mysh executable.

--------------------------------------------------------------------------------------------------------

RUNNING

Interactive mode:

./mysh

Batch mode using a file:

./mysh batch.txt

Batch mode using standard input:

./mysh < batch.txt

Example interactive commands:

pwd  
cd /tmp  
which ls  
echo hello  
echo hello | cat  
echo *.c  
exit

Normal command output goes to standard output.

Error messages and interactive failure reports go to standard error.

--------------------------------------------------------------------------------------------------------