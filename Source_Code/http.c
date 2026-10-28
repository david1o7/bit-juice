#define _GNU_SOURCE
#include "../Header_files/http.h"
#include "../Header_files/server.h"

#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <sys/stat.h>

