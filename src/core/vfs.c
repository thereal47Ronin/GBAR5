/* PSXS5 - the file system the emulators are given (libretro VFS v3): local
 * files as stdio does them, games on network shares (smb://, nfs://) through
 * netfs.c, read-only. Beetle and SwanStation ask it only for scheme:// paths;
 * PCSX-ReARMed for all of its files.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "vfs.h"

#include "../netfs.h"
#include "../psxs5.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct retro_vfs_file_handle
{
    FILE *local;
    VFile *net;
    char path[PSXS5_PATH_MAX];
};

struct retro_vfs_dir_handle
{
    VDir *dir;
    const char *name;
    bool is_dir;
};

static const char *RETRO_CALLCONV vfs_get_path(struct retro_vfs_file_handle *h)
{
    return h->path;
}

static struct retro_vfs_file_handle *RETRO_CALLCONV vfs_open(const char *path, unsigned mode, unsigned hints)
{
    (void)hints;
    if (!path || !*path)
        return NULL;
    struct retro_vfs_file_handle *h = calloc(1, sizeof(*h));
    if (!h)
        return NULL;
    str_copy(h->path, sizeof(h->path), path);
    if (netfs_is_url(path))
    {
        if (mode != RETRO_VFS_FILE_ACCESS_READ || !(h->net = vf_open(path)))
        {
            free(h);
            return NULL;
        }
        return h;
    }
    const char *how = "rb";
    bool keep = (mode & RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) != 0;
    if ((mode & RETRO_VFS_FILE_ACCESS_READ_WRITE) == RETRO_VFS_FILE_ACCESS_READ_WRITE)
        how = keep ? "r+b" : "w+b";
    else if (mode & RETRO_VFS_FILE_ACCESS_WRITE)
        how = keep ? "r+b" : "wb";
    h->local = fopen(path, how);
    if (!h->local)
    {
        free(h);
        return NULL;
    }
    return h;
}

static int RETRO_CALLCONV vfs_close(struct retro_vfs_file_handle *h)
{
    if (!h)
        return -1;
    int r = 0;
    if (h->local)
        r = fclose(h->local) == 0 ? 0 : -1;
    vf_close(h->net);
    free(h);
    return r;
}

static int64_t RETRO_CALLCONV vfs_size(struct retro_vfs_file_handle *h)
{
    if (h->net)
        return vf_size(h->net);
    off_t at = ftello(h->local);
    if (fseeko(h->local, 0, SEEK_END) != 0)
        return -1;
    off_t size = ftello(h->local);
    fseeko(h->local, at, SEEK_SET);
    return (int64_t)size;
}

static int64_t RETRO_CALLCONV vfs_truncate(struct retro_vfs_file_handle *h, int64_t length)
{
    if (h->net)
        return -1;
    fflush(h->local);
    return ftruncate(fileno(h->local), (off_t)length) == 0 ? 0 : -1;
}

static int64_t RETRO_CALLCONV vfs_tell(struct retro_vfs_file_handle *h)
{
    return h->net ? vf_tell(h->net) : (int64_t)ftello(h->local);
}

static int64_t RETRO_CALLCONV vfs_seek(struct retro_vfs_file_handle *h, int64_t offset, int whence)
{
    if (h->net)
    {
        int64_t to = whence == RETRO_VFS_SEEK_POSITION_CURRENT ? vf_tell(h->net) + offset
                     : whence == RETRO_VFS_SEEK_POSITION_END   ? vf_size(h->net) + offset
                                                               : offset;
        return vf_seek(h->net, to) ? to : -1;
    }
    int how = whence == RETRO_VFS_SEEK_POSITION_CURRENT ? SEEK_CUR : whence == RETRO_VFS_SEEK_POSITION_END ? SEEK_END : SEEK_SET;
    if (fseeko(h->local, (off_t)offset, how) != 0)
        return -1;
    return (int64_t)ftello(h->local);
}

static int64_t RETRO_CALLCONV vfs_read(struct retro_vfs_file_handle *h, void *s, uint64_t len)
{
    if (h->net)
        return (int64_t)vf_read(h->net, s, (size_t)len);
    size_t got = fread(s, 1, (size_t)len, h->local);
    return got == 0 && ferror(h->local) ? -1 : (int64_t)got;
}

static int64_t RETRO_CALLCONV vfs_write(struct retro_vfs_file_handle *h, const void *s, uint64_t len)
{
    if (h->net)
        return -1;
    size_t put = fwrite(s, 1, (size_t)len, h->local);
    return put == 0 && len ? -1 : (int64_t)put;
}

static int RETRO_CALLCONV vfs_flush(struct retro_vfs_file_handle *h)
{
    return h->net ? 0 : (fflush(h->local) == 0 ? 0 : -1);
}

static int RETRO_CALLCONV vfs_remove(const char *path)
{
    return netfs_is_url(path) ? -1 : (remove(path) == 0 ? 0 : -1);
}

static int RETRO_CALLCONV vfs_rename(const char *from, const char *to)
{
    if (netfs_is_url(from) || netfs_is_url(to))
        return -1;
    return rename(from, to) == 0 ? 0 : -1;
}

static int RETRO_CALLCONV vfs_stat(const char *path, int32_t *size)
{
    if (netfs_is_url(path))
    {
        if (vf_is_dir(path))
            return RETRO_VFS_STAT_IS_VALID | RETRO_VFS_STAT_IS_DIRECTORY;
        VFile *f = vf_open(path);
        if (!f)
            return 0;
        if (size)
            *size = (int32_t)vf_size(f);
        vf_close(f);
        return RETRO_VFS_STAT_IS_VALID;
    }
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    if (size)
        *size = (int32_t)st.st_size;
    return RETRO_VFS_STAT_IS_VALID | (S_ISDIR(st.st_mode) ? RETRO_VFS_STAT_IS_DIRECTORY : 0);
}

static int RETRO_CALLCONV vfs_mkdir(const char *dir)
{
    if (netfs_is_url(dir))
        return -1;
    if (mkdir(dir, 0777) == 0)
        return 0;
    return errno == EEXIST ? -2 : -1;
}

static struct retro_vfs_dir_handle *RETRO_CALLCONV vfs_opendir(const char *dir, bool include_hidden)
{
    (void)include_hidden;
    VDir *d = vd_open(dir);
    if (!d)
        return NULL;
    struct retro_vfs_dir_handle *h = calloc(1, sizeof(*h));
    if (!h)
    {
        vd_close(d);
        return NULL;
    }
    h->dir = d;
    return h;
}

static bool RETRO_CALLCONV vfs_readdir(struct retro_vfs_dir_handle *h)
{
    h->name = vd_next(h->dir, &h->is_dir);
    return h->name != NULL;
}

static const char *RETRO_CALLCONV vfs_dirent_get_name(struct retro_vfs_dir_handle *h)
{
    return h->name;
}

static bool RETRO_CALLCONV vfs_dirent_is_dir(struct retro_vfs_dir_handle *h)
{
    return h->is_dir;
}

static int RETRO_CALLCONV vfs_closedir(struct retro_vfs_dir_handle *h)
{
    if (!h)
        return -1;
    vd_close(h->dir);
    free(h);
    return 0;
}

static struct retro_vfs_interface vfs_iface = {
    vfs_get_path, vfs_open,    vfs_close, vfs_size,    vfs_tell,    vfs_seek,           vfs_read,
    vfs_write,    vfs_flush,   vfs_remove, vfs_rename, vfs_truncate, vfs_stat,          vfs_mkdir,
    vfs_opendir,  vfs_readdir, vfs_dirent_get_name, vfs_dirent_is_dir, vfs_closedir,
};

bool vfs_interface(struct retro_vfs_interface_info *info)
{
    if (!info || info->required_interface_version > 3)
        return false;
    info->required_interface_version = 3;
    info->iface = &vfs_iface;
    return true;
}
