#ifndef MAIN_H_INCLUDED
#define MAIN_H_INCLUDED

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <sys/wait.h>
#include <limits.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

typedef struct {
    int fd;
    char *file_path;
    int line_to_read;
} file_arg;

void ems_file_reader(char* file_path, unsigned int MAX_THREADS);

void* ems_command_processing(void* file_struct);

#endif  