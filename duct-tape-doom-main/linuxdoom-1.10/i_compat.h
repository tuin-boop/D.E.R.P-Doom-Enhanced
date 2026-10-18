/* Portable file APIs used by the original engine. */
#ifndef DOOM_I_COMPAT_H
#define DOOM_I_COMPAT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <malloc.h>
#define open _open
#define close _close
#define read _read
#define write _write
#define lseek _lseek
#define access _access
#define fstat _fstat
#define stat _stat
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#ifndef alloca
#define alloca _alloca
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef O_BINARY
#define O_BINARY _O_BINARY
#endif
#else
#include <unistd.h>
#include <strings.h>
#include <alloca.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif
#endif
