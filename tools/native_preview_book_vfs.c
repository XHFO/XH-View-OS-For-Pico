/* SPDX-License-Identifier: Apache-2.0
 * 中文：仅替换文件系统平台边界，不改变真实阅读器、批注、封面和存储规则。
 * English: Replace only the filesystem platform boundary, preserving actual reader, annotation, cover and storage rules.
 */
#include "native_preview_hal.h"
#include "native_preview_book_vfs.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdarg.h>
#undef fopen
#undef opendir
#undef stat
#undef lstat
#undef mkdir
#undef unlink
#undef remove
#undef rmdir
#undef rename
#undef open
#undef access
#undef readlink
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
static const char *mapped(const char *path,char out[PATH_MAX]) {
    if (!path) { errno=EINVAL;return NULL; }
    const char *part=NULL,*directory=NULL;
    if (!strncmp(path,"/sdcard",7) && (!path[7] || path[7]=='/')) { part=path+7;directory="tf"; }
    else if (!strncmp(path,"/flash",6) && (!path[6] || path[6]=='/')) { part=path+6;directory="flash"; }
    if (!part) return path;
    if (snprintf(out,PATH_MAX,"%s/%s%s",native_preview_root(),directory,part)>=PATH_MAX) { errno=ENAMETOOLONG;return NULL; }
    return out;
}
FILE *native_preview_vfs_fopen(const char *path,const char *mode) { char p[PATH_MAX];const char *s=mapped(path,p);return s?fopen(s,mode):NULL; }
DIR *native_preview_vfs_opendir(const char *path) { char p[PATH_MAX];const char *s=mapped(path,p);return s?opendir(s):NULL; }
int native_preview_vfs_stat(const char *path,struct stat *out) { char p[PATH_MAX];const char *s=mapped(path,p);return s?stat(s,out):-1; }
int native_preview_vfs_lstat(const char *path,struct stat *out) { char p[PATH_MAX];const char *s=mapped(path,p);return s?lstat(s,out):-1; }
int native_preview_vfs_mkdir(const char *path,mode_t mode) { char p[PATH_MAX];const char *s=mapped(path,p);return s?mkdir(s,mode):-1; }
int native_preview_vfs_unlink(const char *path) { char p[PATH_MAX];const char *s=mapped(path,p);return s?unlink(s):-1; }
int native_preview_vfs_remove(const char *path) { char p[PATH_MAX];const char *s=mapped(path,p);return s?remove(s):-1; }
int native_preview_vfs_rmdir(const char *path) { char p[PATH_MAX];const char *s=mapped(path,p);return s?rmdir(s):-1; }
int native_preview_vfs_rename(const char *from,const char *to) { char a[PATH_MAX],b[PATH_MAX];const char *x=mapped(from,a),*y=mapped(to,b);return x&&y?rename(x,y):-1; }
int native_preview_vfs_open(const char *path,int flags,...) {
    char p[PATH_MAX];const char *s=mapped(path,p);if(!s)return -1;
    if(flags&O_CREAT){va_list args;va_start(args,flags);int mode=va_arg(args,int);va_end(args);return open(s,flags,mode);}
    return open(s,flags);
}
int native_preview_vfs_access(const char *path,int mode) { char p[PATH_MAX];const char *s=mapped(path,p);return s?access(s,mode):-1; }
ssize_t native_preview_vfs_readlink(const char *path,char *out,size_t bytes) { char p[PATH_MAX];const char *s=mapped(path,p);return s?readlink(s,out,bytes):-1; }
