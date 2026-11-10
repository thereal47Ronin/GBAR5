/* PSXS5 - games on network shares: SMB 2/3 (Windows, NAS) and NFS, read-only.
 *
 * The shares are listed in <data>/network.cfg, one a line:
 *   smb://192.168.1.10/Games/PS1  user=me password=secret
 *   nfs://192.168.1.10/volume1/ps1
 * A game on one is a path like smb://192.168.1.10/Games/PS1/Crash/Crash.chd:
 * the emulators open it through the frontend's file system (host.c), the
 * library and the disc reader through VFile and VDir below, which also take
 * local paths. Passwords stay in network.cfg; they are never in a path. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NETFS_MAX_SHARES 8

void netfs_load(const char *config_path);
int netfs_share_count(void);
const char *netfs_share_url(int index);    /* without the password */
const char *netfs_share_status(int index); /* "" until tried, then "connected" or why not */
bool netfs_is_url(const char *path);       /* smb:// or nfs:// */
void netfs_shutdown(void);

/* a file read-only, local or on a share */
typedef struct VFile VFile;
VFile *vf_open(const char *path);
size_t vf_read(VFile *f, void *buf, size_t len);
bool vf_seek(VFile *f, int64_t pos); /* from the start */
int64_t vf_tell(VFile *f);
int64_t vf_size(VFile *f);
char *vf_gets(char *line, int size, VFile *f);
void vf_close(VFile *f);

/* a folder's names, local or on a share */
typedef struct VDir VDir;
VDir *vd_open(const char *path);
const char *vd_next(VDir *d, bool *is_dir); /* NULL at the end */
void vd_close(VDir *d);
bool vf_is_dir(const char *path);
bool vf_exists(const char *path);
