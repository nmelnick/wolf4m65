// Stub: enough of <sys/stat.h> for Wolf4SDL's "does this file exist" checks.
// stat() is implemented in m65_posix.c. (mkdir comes from <unistd.h>.)
#ifndef M65_SYS_STAT_H
#define M65_SYS_STAT_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
struct stat { long st_size; };
int stat(const char *path, struct stat *buf);
#ifdef __cplusplus
}
#endif
#endif
