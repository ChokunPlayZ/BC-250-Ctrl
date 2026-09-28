#include "shell_args.h"

#include <ctype.h>

int bc250_shell_split(char *line, char **argv, size_t capacity)
{
    char *read = line;
    char *write = line;
    size_t count = 0;
    while (*read) {
        while (isspace((unsigned char)*read)) ++read;
        if (!*read) break;
        if (count == capacity) return -1;
        argv[count++] = write;
        char quote = 0;
        while (*read && (quote || !isspace((unsigned char)*read))) {
            char ch = *read++;
            if (ch == '\\' && quote != '\'') {
                if (!*read) return -1;
                *write++ = *read++;
            } else if (ch == quote) {
                quote = 0;
            } else if (!quote && (ch == '\'' || ch == '"')) {
                quote = ch;
            } else {
                *write++ = ch;
            }
        }
        if (quote) return -1;
        /* Advance before writing the terminator: read and write may coincide. */
        while (isspace((unsigned char)*read)) ++read;
        *write++ = '\0';
    }
    return (int)count;
}
