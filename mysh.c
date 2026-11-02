#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char const *argv[])
{
    if (argc > 2)
    {
        char msg[] = "No more than 1 argument allowed.\n";
        write(STDERR_FILENO, msg, sizeof(msg) - 1);
        exit(EXIT_FAILURE);
    }
    char buf[BUFSIZ];
    size_t buflen = 0;
    ssize_t bytes;
    int fd = STDIN_FILENO;
    // batch mode
    if (argc == 2)
    {
        fd = open(argv[1], O_RDONLY);
        if (fd < 0)
        {
            perror("open");
            exit(EXIT_FAILURE);
        }
    }
    // interactive mode
    if (argc == 1 && isatty(fd))
    {
        printf("Weclome to my shell!\n");
    }
    char c;
    while ((bytes = read(fd, &c, 1)) > 0)
    {
        if (c == '\n')
        {
            buf[buflen] = '\0';
            // process command here
            buflen = 0;
            continue;
        }
        else
        {
            buf[buflen] = c;
            buflen++;
        }
    }
    if (bytes == -1)
    {
        perror("read");
        exit(EXIT_FAILURE);
    }
    if (bytes == 0 && buflen > 0)
    {
        buf[buflen] = '\0';
        // process command
        buflen = 0;
    }
}
