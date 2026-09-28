#pragma once

#include <stddef.h>

/* Splits in place, with shell-style quotes and backslash escapes. Returns -1 on error. */
int bc250_shell_split(char *line, char **argv, size_t capacity);

