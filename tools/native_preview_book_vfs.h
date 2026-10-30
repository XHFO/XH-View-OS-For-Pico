/* SPDX-License-Identifier: Apache-2.0
 * 中文：固件的 /sdcard 路径映射到预览会话私有 TF 目录，生产路径与验证逻辑保持不变。
 * English: Map firmware /sdcard paths to the preview session's private TF directory, retaining production paths and validation.
 */
#pragma once
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
int native_preview_vfs_open(const char *path,int flags,...);
int native_preview_vfs_access(const char *path,int mode);
ssize_t native_preview_vfs_readlink(const char *path,char *out,size_t bytes);
FILE *native_preview_vfs_fopen(const char *path,const char *mode);
DIR *native_preview_vfs_opendir(const char *path);
int native_preview_vfs_stat(const char *path,struct stat *out);
int native_preview_vfs_lstat(const char *path,struct stat *out);
int native_preview_vfs_mkdir(const char *path,mode_t mode);
int native_preview_vfs_unlink(const char *path);
int native_preview_vfs_remove(const char *path);
int native_preview_vfs_rmdir(const char *path);
int native_preview_vfs_rename(const char *from,const char *to);
#define fopen(...) native_preview_vfs_fopen(__VA_ARGS__)
#define opendir(...) native_preview_vfs_opendir(__VA_ARGS__)
#define stat(...) native_preview_vfs_stat(__VA_ARGS__)
#define lstat(...) native_preview_vfs_lstat(__VA_ARGS__)
#define mkdir(...) native_preview_vfs_mkdir(__VA_ARGS__)
#define unlink(...) native_preview_vfs_unlink(__VA_ARGS__)
#define remove(...) native_preview_vfs_remove(__VA_ARGS__)
#define rmdir(...) native_preview_vfs_rmdir(__VA_ARGS__)
#define rename(...) native_preview_vfs_rename(__VA_ARGS__)
#define open(...) native_preview_vfs_open(__VA_ARGS__)
#define access(...) native_preview_vfs_access(__VA_ARGS__)
#define readlink(...) native_preview_vfs_readlink(__VA_ARGS__)
