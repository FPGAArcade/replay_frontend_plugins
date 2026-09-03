// Replay Frontend port definitions for ScummVM
// This file is included by scummsys.h when NONSTANDARD_PORT is defined

#ifndef PORTDEFS_H
#define PORTDEFS_H

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <new>
#include <limits>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#ifdef POSIX
#include <fcntl.h>
#include <unistd.h>
#endif

// Default resolution for overlay
#define RES_W_OVERLAY 640
#define RES_H_OVERLAY 480
#define RES_INIT_MAX_W 640
#define RES_INIT_MAX_H 480

#endif // PORTDEFS_H
