/* PSXS5 - games on network shares (netfs.h): libsmb2 and libnfs, synchronous,
 * one connection per share kept open, a lock per share (the emulators read
 * from their own threads). A failed read connects again once and retries.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "netfs.h"

#include "psxs5.h"

#include <SDL2/SDL.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(PSXS5_HAVE_NETFS)
#include <nfsc/libnfs.h>
#include <smb2/smb2.h> /* before libsmb2.h, which uses its types */
#include <smb2/libsmb2.h>
#endif

enum
{
    SHARE_SMB,
    SHARE_NFS
};

typedef struct
{
    int kind;
    char url[256];   /* smb://host/share[/folder] or nfs://host/export, no trailing / */
    char host[128];
    char share[128]; /* SMB: the share's name; NFS: the export, from its / */
    char base[256];  /* SMB: the folder inside the share ("" for its root) */
    char user[64], password[128], domain[64];
    char status[128];
    void *ctx; /* struct smb2_context * or struct nfs_context * */
    SDL_mutex *lock;
} Share;

static Share shares[NETFS_MAX_SHARES];
static int share_count;

static void copy_n(char *dst, size_t size, const char *src, size_t n)
{
    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* ---------------------------------------------------------------- shares */

static bool parse_share(Share *s, char *line)
{
    memset(s, 0, sizeof(*s));
    char *url = strtok(line, " \t");
    if (!url)
        return false;
    if (!strncmp(url, "smb://", 6))
        s->kind = SHARE_SMB;
    else if (!strncmp(url, "nfs://", 6))
        s->kind = SHARE_NFS;
    else
        return false;
    size_t len = strlen(url);
    while (len > 6 && url[len - 1] == '/')
        url[--len] = '\0';
    str_copy(s->url, sizeof(s->url), url);
    const char *rest = url + 6;
    const char *slash = strchr(rest, '/');
    if (!slash || slash == rest)
        return false;
    copy_n(s->host, sizeof(s->host), rest, (size_t)(slash - rest));
    if (s->kind == SHARE_NFS)
        str_copy(s->share, sizeof(s->share), slash); /* the whole path is the export */
    else
    {
        const char *name = slash + 1;
        const char *next = strchr(name, '/');
        copy_n(s->share, sizeof(s->share), name, next ? (size_t)(next - name) : strlen(name));
        if (next)
            str_copy(s->base, sizeof(s->base), next + 1);
        if (!s->share[0])
            return false;
    }
    for (char *kv; (kv = strtok(NULL, " \t"));)
    {
        char *eq = strchr(kv, '=');
        if (!eq)
            continue;
        *eq = '\0';
        if (!strcmp(kv, "user"))
            str_copy(s->user, sizeof(s->user), eq + 1);
        else if (!strcmp(kv, "password"))
            str_copy(s->password, sizeof(s->password), eq + 1);
        else if (!strcmp(kv, "domain"))
            str_copy(s->domain, sizeof(s->domain), eq + 1);
    }
    s->lock = SDL_CreateMutex();
    return true;
}

void netfs_load(const char *config_path)
{
    netfs_shutdown();
    FILE *f = fopen(config_path, "r");
    if (!f)
        return;
    char line[700];
    while (fgets(line, sizeof(line), f) && share_count < NETFS_MAX_SHARES)
    {
        line[strcspn(line, "\r\n")] = '\0';
        char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!*p || *p == '#')
            continue;
        if (parse_share(&shares[share_count], p))
        {
            psxs5_log("netfs: share %s%s", shares[share_count].url,
                      shares[share_count].user[0] ? " (with a user)" : "");
            ++share_count;
        }
        else
            psxs5_log("netfs: a line of %s isn't a share (smb://host/share or nfs://host/export)", config_path);
    }
    fclose(f);
}

int netfs_share_count(void)
{
    return share_count;
}

const char *netfs_share_url(int index)
{
    return index >= 0 && index < share_count ? shares[index].url : "";
}

const char *netfs_share_status(int index)
{
    return index >= 0 && index < share_count ? shares[index].status : "";
}

bool netfs_is_url(const char *path)
{
    return path && (!strncmp(path, "smb://", 6) || !strncmp(path, "nfs://", 6));
}

/* The share a path is on, and the path inside it (SMB: no leading /; NFS: from /) */
static Share *share_for(const char *path, char *inside, size_t size)
{
    for (int i = 0; i < share_count; ++i)
    {
        size_t n = strlen(shares[i].url);
        if (strncmp(path, shares[i].url, n) != 0 || (path[n] != '/' && path[n] != '\0'))
            continue;
        const char *rest = path + n; /* "" or "/..." */
        if (shares[i].kind == SHARE_NFS)
            str_copy(inside, size, rest[0] ? rest : "/");
        else if (shares[i].base[0])
            snprintf(inside, size, "%s%s", shares[i].base, rest);
        else
            str_copy(inside, size, rest[0] ? rest + 1 : "");
        return &shares[i];
    }
    return NULL;
}

#if defined(PSXS5_HAVE_NETFS)
static void share_drop(Share *s)
{
    if (!s->ctx)
        return;
    if (s->kind == SHARE_SMB)
    {
        smb2_disconnect_share(s->ctx);
        smb2_destroy_context(s->ctx);
    }
    else
        nfs_destroy_context(s->ctx);
    s->ctx = NULL;
}

/* with the share's lock held */
static bool share_connect(Share *s)
{
    if (s->ctx)
        return true;
    if (s->kind == SHARE_SMB)
    {
        struct smb2_context *c = smb2_init_context();
        if (!c)
            return false;
        smb2_set_timeout(c, 5);
        smb2_set_security_mode(c, SMB2_NEGOTIATE_SIGNING_ENABLED);
        smb2_set_user(c, s->user[0] ? s->user : "guest");
        smb2_set_password(c, s->password);
        if (s->domain[0])
            smb2_set_domain(c, s->domain);
        if (smb2_connect_share(c, s->host, s->share, s->user[0] ? s->user : "guest") < 0)
        {
            snprintf(s->status, sizeof(s->status), "%s", smb2_get_error(c));
            psxs5_log("netfs: %s: %s", s->url, s->status);
            smb2_destroy_context(c);
            return false;
        }
        s->ctx = c;
    }
    else
    {
        struct nfs_context *c = nfs_init_context();
        if (!c)
            return false;
        nfs_set_timeout(c, 5000);
        if (nfs_mount(c, s->host, s->share) != 0)
        {
            snprintf(s->status, sizeof(s->status), "%s", nfs_get_error(c));
            psxs5_log("netfs: %s: %s", s->url, s->status);
            nfs_destroy_context(c);
            return false;
        }
        s->ctx = c;
    }
    str_copy(s->status, sizeof(s->status), "connected");
    psxs5_log("netfs: %s connected", s->url);
    return true;
}

static void *net_open(Share *s, const char *inside, int64_t *size)
{
    if (!share_connect(s))
        return NULL;
    if (s->kind == SHARE_SMB)
    {
        struct smb2fh *fh = smb2_open(s->ctx, inside, O_RDONLY);
        struct smb2_stat_64 st;
        if (fh && smb2_fstat(s->ctx, fh, &st) == 0)
            *size = (int64_t)st.smb2_size;
        return fh;
    }
    struct nfsfh *fh = NULL;
    struct nfs_stat_64 st;
    if (nfs_open(s->ctx, inside, O_RDONLY, &fh) != 0)
        return NULL;
    if (nfs_fstat64(s->ctx, fh, &st) == 0)
        *size = (int64_t)st.nfs_size;
    return fh;
}

static void net_close(Share *s, void *fh)
{
    if (!s->ctx || !fh)
        return;
    if (s->kind == SHARE_SMB)
        smb2_close(s->ctx, fh);
    else
        nfs_close(s->ctx, fh);
}

/* reads up to len bytes at pos; -1 on an error */
static int64_t net_pread(Share *s, void *fh, uint8_t *buf, size_t len, int64_t pos)
{
    size_t done = 0;
    while (done < len)
    {
        size_t want = len - done;
        int got;
        if (s->kind == SHARE_SMB)
        {
            uint32_t most = smb2_get_max_read_size(s->ctx);
            if (most && want > most)
                want = most;
            got = smb2_pread(s->ctx, fh, buf + done, (uint32_t)want, (uint64_t)(pos + done));
        }
        else
        {
            size_t most = nfs_get_readmax(s->ctx);
            if (most && want > most)
                want = most;
            got = nfs_pread(s->ctx, fh, buf + done, want, (uint64_t)(pos + done));
        }
        if (got < 0)
            return done ? (int64_t)done : -1;
        if (got == 0)
            break;
        done += (size_t)got;
    }
    return (int64_t)done;
}
#endif

void netfs_shutdown(void)
{
#if defined(PSXS5_HAVE_NETFS)
    for (int i = 0; i < share_count; ++i)
    {
        share_drop(&shares[i]);
        if (shares[i].lock)
            SDL_DestroyMutex(shares[i].lock);
    }
#endif
    memset(shares, 0, sizeof(shares));
    share_count = 0;
}

/* ---------------------------------------------------------------- files */

struct VFile
{
    FILE *local;
    Share *share;
    void *fh;
    char inside[PSXS5_PATH_MAX];
    int64_t pos, size;
    uint8_t buf[4096]; /* for vf_gets on a share */
    int64_t buf_at;
    size_t buf_len;
};

VFile *vf_open(const char *path)
{
    VFile *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    if (!netfs_is_url(path))
    {
        f->local = fopen(path, "rb");
        if (!f->local)
        {
            free(f);
            return NULL;
        }
        return f;
    }
#if defined(PSXS5_HAVE_NETFS)
    f->share = share_for(path, f->inside, sizeof(f->inside));
    if (f->share)
    {
        SDL_LockMutex(f->share->lock);
        f->fh = net_open(f->share, f->inside, &f->size);
        if (!f->fh && f->share->ctx)
        {
            share_drop(f->share); /* a connection that went stale: once more */
            f->fh = net_open(f->share, f->inside, &f->size);
        }
        SDL_UnlockMutex(f->share->lock);
    }
    if (f->fh)
        return f;
#endif
    free(f);
    return NULL;
}

size_t vf_read(VFile *f, void *buf, size_t len)
{
    if (f->local)
        return fread(buf, 1, len, f->local);
#if defined(PSXS5_HAVE_NETFS)
    SDL_LockMutex(f->share->lock);
    int64_t got = f->share->ctx ? net_pread(f->share, f->fh, buf, len, f->pos) : -1;
    if (got < 0)
    {
        /* the connection dropped: connect, open the file again, read again */
        share_drop(f->share);
        int64_t size = f->size;
        f->fh = net_open(f->share, f->inside, &size);
        got = f->fh ? net_pread(f->share, f->fh, buf, len, f->pos) : -1;
    }
    SDL_UnlockMutex(f->share->lock);
    if (got <= 0)
        return 0;
    f->pos += got;
    return (size_t)got;
#else
    (void)buf, (void)len;
    return 0;
#endif
}

bool vf_seek(VFile *f, int64_t pos)
{
    if (f->local)
        return fseeko(f->local, (off_t)pos, SEEK_SET) == 0;
    if (pos < 0)
        return false;
    f->pos = pos;
    return true;
}

int64_t vf_tell(VFile *f)
{
    return f->local ? (int64_t)ftello(f->local) : f->pos;
}

int64_t vf_size(VFile *f)
{
    if (!f->local)
        return f->size;
    off_t at = ftello(f->local);
    fseeko(f->local, 0, SEEK_END);
    off_t size = ftello(f->local);
    fseeko(f->local, at, SEEK_SET);
    return (int64_t)size;
}

char *vf_gets(char *line, int size, VFile *f)
{
    if (f->local)
        return fgets(line, size, f->local);
    int n = 0;
    while (n < size - 1)
    {
        if (f->pos < f->buf_at || f->pos >= f->buf_at + (int64_t)f->buf_len)
        {
            int64_t at = f->pos;
            f->buf_at = at;
            f->buf_len = 0;
            size_t got = vf_read(f, f->buf, sizeof(f->buf));
            f->pos = at;
            f->buf_len = got;
            if (got == 0)
                break;
        }
        char c = (char)f->buf[f->pos - f->buf_at];
        ++f->pos;
        line[n++] = c;
        if (c == '\n')
            break;
    }
    line[n] = '\0';
    return n ? line : NULL;
}

void vf_close(VFile *f)
{
    if (!f)
        return;
    if (f->local)
        fclose(f->local);
#if defined(PSXS5_HAVE_NETFS)
    else if (f->share)
    {
        SDL_LockMutex(f->share->lock);
        net_close(f->share, f->fh);
        SDL_UnlockMutex(f->share->lock);
    }
#endif
    free(f);
}

/* ---------------------------------------------------------------- folders */

typedef struct
{
    char *name;
    bool dir;
} Entry;

struct VDir
{
    DIR *local;
    char path[PSXS5_PATH_MAX];
    Entry *entries;
    int count, at;
};

static void add_entry(VDir *d, const char *name, bool dir)
{
    if (!strcmp(name, ".") || !strcmp(name, ".."))
        return;
    if (d->count % 64 == 0)
    {
        Entry *more = realloc(d->entries, sizeof(Entry) * (size_t)(d->count + 64));
        if (!more)
            return;
        d->entries = more;
    }
    d->entries[d->count].name = strdup(name);
    d->entries[d->count].dir = dir;
    if (d->entries[d->count].name)
        ++d->count;
}

VDir *vd_open(const char *path)
{
    VDir *d = calloc(1, sizeof(*d));
    if (!d)
        return NULL;
    str_copy(d->path, sizeof(d->path), path);
    if (!netfs_is_url(path))
    {
        d->local = opendir(path);
        if (!d->local)
        {
            free(d);
            return NULL;
        }
        return d;
    }
#if defined(PSXS5_HAVE_NETFS)
    char inside[PSXS5_PATH_MAX];
    Share *s = share_for(path, inside, sizeof(inside));
    bool ok = false;
    if (s)
    {
        SDL_LockMutex(s->lock);
        for (int attempt = 0; attempt < 2 && !ok; ++attempt)
        {
            if (attempt)
                share_drop(s);
            if (!share_connect(s))
                break;
            if (s->kind == SHARE_SMB)
            {
                struct smb2dir *dir = smb2_opendir(s->ctx, inside);
                if (dir)
                {
                    struct smb2dirent *e;
                    while ((e = smb2_readdir(s->ctx, dir)))
                        add_entry(d, e->name, e->st.smb2_type == SMB2_TYPE_DIRECTORY);
                    smb2_closedir(s->ctx, dir);
                    ok = true;
                }
            }
            else
            {
                struct nfsdir *dir = NULL;
                if (nfs_opendir(s->ctx, inside, &dir) == 0)
                {
                    struct nfsdirent *e;
                    while ((e = nfs_readdir(s->ctx, dir)))
                        add_entry(d, e->name, e->type == 2 /* NF3DIR */);
                    nfs_closedir(s->ctx, dir);
                    ok = true;
                }
            }
        }
        SDL_UnlockMutex(s->lock);
    }
    if (ok)
        return d;
#endif
    vd_close(d);
    return NULL;
}

const char *vd_next(VDir *d, bool *is_dir)
{
    if (d->local)
    {
        struct dirent *e;
        while ((e = readdir(d->local)))
        {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            if (is_dir)
            {
                char full[PSXS5_PATH_MAX];
                path_join(full, sizeof(full), d->path, e->d_name);
                *is_dir = path_is_dir(full);
            }
            return e->d_name;
        }
        return NULL;
    }
    if (d->at >= d->count)
        return NULL;
    if (is_dir)
        *is_dir = d->entries[d->at].dir;
    return d->entries[d->at++].name;
}

void vd_close(VDir *d)
{
    if (!d)
        return;
    if (d->local)
        closedir(d->local);
    for (int i = 0; i < d->count; ++i)
        free(d->entries[i].name);
    free(d->entries);
    free(d);
}

#if defined(PSXS5_HAVE_NETFS)
static bool net_stat(const char *path, bool *dir)
{
    char inside[PSXS5_PATH_MAX];
    Share *s = share_for(path, inside, sizeof(inside));
    if (!s)
        return false;
    bool ok = false;
    SDL_LockMutex(s->lock);
    if (share_connect(s))
    {
        if (s->kind == SHARE_SMB)
        {
            struct smb2_stat_64 st;
            ok = smb2_stat(s->ctx, inside, &st) == 0;
            *dir = ok && st.smb2_type == SMB2_TYPE_DIRECTORY;
        }
        else
        {
            struct nfs_stat_64 st;
            ok = nfs_stat64(s->ctx, inside, &st) == 0;
            *dir = ok && S_ISDIR(st.nfs_mode);
        }
    }
    SDL_UnlockMutex(s->lock);
    return ok;
}
#endif

bool vf_is_dir(const char *path)
{
    if (!netfs_is_url(path))
        return path_is_dir(path);
#if defined(PSXS5_HAVE_NETFS)
    bool dir = false;
    return net_stat(path, &dir) && dir;
#else
    return false;
#endif
}

bool vf_exists(const char *path)
{
    if (!netfs_is_url(path))
        return path_exists(path);
#if defined(PSXS5_HAVE_NETFS)
    bool dir = false;
    return net_stat(path, &dir);
#else
    return false;
#endif
}
