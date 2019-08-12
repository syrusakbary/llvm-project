#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <wasi/core.h>

#include "stb_sprintf.h"

#define TRACE 1

#define WASM_EXPORT __attribute__((__visibility__("default")))
#define MAX_FDS 4096
#define MAX_NODES 1024
#define MAX_PATH 8192
#define MIN_PRESTAT_FDS 3
#define INVALID_INODE ((__wasi_inode_t)~0)

extern void memfs_log(const void* buf, size_t buf_size);
extern __wasi_errno_t host_write(__wasi_fd_t fd, const __wasi_ciovec_t *iovs,
                                 size_t iovs_len, size_t *nwritten);
extern void copy_out(void* their_dest, const void* my_src, size_t size);
extern void copy_in(void* my_dest, const void* their_src, size_t size);

static void __attribute__((format(printf, 1, 2))) logf(const char *fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  size_t buf_len = stbsp_vsnprintf(buf, sizeof(buf), fmt, args);
  memfs_log(buf, buf_len);
  va_end(args);
}

void Assert(bool result, const char* cond) {
  if (!result) {
    logf("Assertion failed: %s", cond);
    abort();
  }
}

#define ASSERT(cond) Assert(cond, #cond)

#if TRACE
#define tracef(...) logf(__VA_ARGS__)
#else
#define tracef(...) (void)0
#endif

typedef struct FDesc FDesc;
typedef struct Node Node;
typedef struct FileContents FileContents;
typedef struct DirectoryContents DirectoryContents;

struct FileContents {
  void* data;
  __wasi_filesize_t size;
  __wasi_filesize_t capacity;
};

struct DirectoryContents {
  __wasi_dirent_t* dirents;
  size_t size;
};

struct Node {
  union {
    __wasi_inode_t parent;
    __wasi_inode_t next_free;
  };
  char *name;      // Null-terminated.
  size_t name_len; // Length w/o null.
  __wasi_filestat_t stat;
  union {
    FileContents file;
    DirectoryContents dir;
  };
};

struct FDesc {
  __wasi_fdstat_t stat;
  __wasi_inode_t inode;
  bool is_prestat;
};

static const __wasi_device_t kStdinDevice = 0;
static const __wasi_device_t kStdoutDevice = 1;
static const __wasi_device_t kStderrDevice = 2;
static const __wasi_device_t kMemDevice = 3;

static Node g_nodes[MAX_NODES];
static FDesc g_fdescs[MAX_FDS];
static __wasi_inode_t g_next_inode;
static char g_path_buf[MAX_PATH];

static void InitInodes(void) {
  g_next_inode = 0;
  for (__wasi_inode_t i = 0; i < MAX_NODES; ++i) {
    g_nodes[i].next_free = i + 1;
  }
  g_nodes[MAX_NODES - 1].next_free = INVALID_INODE;
}

static Node* GetNode(__wasi_inode_t node) {
  ASSERT(node < MAX_NODES);
  return &g_nodes[node];
}

static __wasi_inode_t GetInode(Node* node) {
  __wasi_inode_t inode = node - g_nodes;
  ASSERT(inode < MAX_NODES);
  return inode;
}

static char* GetDirentName(__wasi_dirent_t* dirent) {
  return (char *)dirent + sizeof(dirent);
}

static __wasi_dirent_t *GetNextDirent(Node* dirnode, __wasi_dirent_t *dirent) {
  if (dirent->d_next < dirnode->dir.size) {
    return (__wasi_dirent_t *)((char *)dirnode->dir.dirents + dirent->d_next);
  }
  return NULL;
}

typedef struct {
  Node* node;
  __wasi_inode_t inode;
} NewNodeResult;

static NewNodeResult NewEmptyNode(void) {
  __wasi_inode_t inode = g_next_inode;
  ASSERT(inode != INVALID_INODE);
  Node* node = &g_nodes[inode];
  g_next_inode = node->next_free;
  return (NewNodeResult){.node = node, .inode = inode};
}

static NewNodeResult NewNode(Node *parent, const char *name,
                             __wasi_filestat_t stat) {
  NewNodeResult result = NewEmptyNode();
  result.node->parent = GetInode(parent ? parent : result.node);
  result.node->name = strdup(name);
  result.node->name_len = strlen(name);
  result.node->stat = stat;
  result.node->stat.st_ino = result.inode;
  return result;
}

static void AddDirent(Node *dirnode, const char *name, size_t name_len,
                      __wasi_inode_t inode) {
  size_t dirent_size = sizeof(__wasi_dirent_t) + name_len + 1;
  size_t old_size = dirnode->dir.size;
  size_t new_size = old_size + dirent_size;
  __wasi_dirent_t* new_dirents = realloc(dirnode->dir.dirents, new_size);
  dirnode->dir.dirents = new_dirents;

  __wasi_dirent_t* dirent = (__wasi_dirent_t*)((char*)new_dirents + old_size);
  dirent->d_next = new_size;
  dirent->d_ino = inode;
  dirent->d_namlen = name_len;
  dirent->d_type = GetNode(inode)->stat.st_filetype;
  char* dirent_name = GetDirentName(dirent);
  memcpy(dirent_name, name, name_len);
  dirent_name[name_len] = 0;

  dirnode->dir.dirents = new_dirents;
  dirnode->dir.size = new_size;
}

typedef struct {
  __wasi_errno_t error;
  FDesc* fdesc;
  __wasi_fd_t fd;
} NewFDResult;

static NewFDResult NewEmptyFD(void) {
  for (__wasi_fd_t i = 0; i < MAX_FDS; ++i) {
    FDesc* fdesc = &g_fdescs[i];
    if (fdesc->stat.fs_filetype == __WASI_FILETYPE_UNKNOWN) {
      return (NewFDResult){.error = __WASI_ESUCCESS, .fdesc = fdesc, .fd = i};
    }
  }
  return (NewFDResult){.error = __WASI_EMFILE, .fdesc = NULL, .fd = 0};
}

static bool IsValidFD(__wasi_fd_t fd) {
  return fd <= MAX_FDS &&
         g_fdescs[fd].stat.fs_filetype != __WASI_FILETYPE_UNKNOWN;
}

static FDesc *GetFDesc(__wasi_fd_t fd) {
  return IsValidFD(fd) ? &g_fdescs[fd] : NULL;
}

static NewFDResult NewFD(Node* node, __wasi_fdstat_t stat, bool is_prestat) {
  ASSERT(node);
  NewFDResult result = NewEmptyFD();
  ASSERT(result.error == __WASI_ESUCCESS);
  result.fdesc->inode = GetInode(node);
  result.fdesc->stat = stat;
  result.fdesc->stat.fs_filetype = node->stat.st_filetype;
  result.fdesc->is_prestat = is_prestat;
  return result;
}

static __wasi_filestat_t GetCharDeviceStat(__wasi_device_t dev) {
  return (__wasi_filestat_t){.st_dev = dev,
                             .st_filetype = __WASI_FILETYPE_CHARACTER_DEVICE,
                             .st_nlink = 1,
                             .st_size = 0,
                             .st_atim = 0,
                             .st_mtim = 0,
                             .st_ctim = 0};
}

static __wasi_filestat_t GetDirectoryStat(void) {
  return (__wasi_filestat_t){.st_dev = kMemDevice,
                             .st_filetype = __WASI_FILETYPE_DIRECTORY,
                             .st_nlink = 1,
                             .st_size = 4096,
                             .st_atim = 0,
                             .st_mtim = 0,
                             .st_ctim = 0};
}

static __wasi_rights_t GetDefaultFileRights(void) {
  return __WASI_RIGHT_FD_DATASYNC | __WASI_RIGHT_FD_READ |
         __WASI_RIGHT_FD_SEEK | __WASI_RIGHT_FD_FDSTAT_SET_FLAGS |
         __WASI_RIGHT_FD_SYNC | __WASI_RIGHT_FD_TELL | __WASI_RIGHT_FD_WRITE |
         __WASI_RIGHT_FD_ADVISE | __WASI_RIGHT_FD_ALLOCATE;
}

static __wasi_fdstat_t GetFileFDStat(void) {
  return (__wasi_fdstat_t){.fs_flags = 0,
                           .fs_rights_base = GetDefaultFileRights(),
                           .fs_rights_inheriting = GetDefaultFileRights()};
}

static __wasi_rights_t GetDefaultDirectoryRights(void) {
  return __WASI_RIGHT_PATH_CREATE_DIRECTORY | __WASI_RIGHT_PATH_CREATE_FILE |
         __WASI_RIGHT_PATH_LINK_SOURCE | __WASI_RIGHT_PATH_LINK_TARGET |
         __WASI_RIGHT_PATH_OPEN | __WASI_RIGHT_FD_READDIR |
         __WASI_RIGHT_PATH_READLINK | __WASI_RIGHT_PATH_RENAME_SOURCE |
         __WASI_RIGHT_PATH_RENAME_TARGET | __WASI_RIGHT_PATH_FILESTAT_GET |
         __WASI_RIGHT_PATH_FILESTAT_SET_SIZE |
         __WASI_RIGHT_PATH_FILESTAT_SET_TIMES | __WASI_RIGHT_FD_FILESTAT_GET |
         __WASI_RIGHT_FD_FILESTAT_SET_SIZE |
         __WASI_RIGHT_FD_FILESTAT_SET_TIMES | __WASI_RIGHT_PATH_SYMLINK |
         __WASI_RIGHT_PATH_REMOVE_DIRECTORY | __WASI_RIGHT_PATH_UNLINK_FILE;
}

static __wasi_fdstat_t GetDirectoryFDStat(void) {
  return (__wasi_fdstat_t){.fs_flags = 0,
                           .fs_rights_base = GetDefaultDirectoryRights(),
                           .fs_rights_inheriting = GetDefaultDirectoryRights()};
}

static void CreateStdFds(void) {
  NewNodeResult in = NewNode(NULL, "stdin", GetCharDeviceStat(kStdinDevice));
  NewFDResult in_fd = NewFD(in.node, GetFileFDStat(), false);
  ASSERT(in_fd.fd == 0);

  NewNodeResult out = NewNode(NULL, "stdout", GetCharDeviceStat(kStdoutDevice));
  NewFDResult out_fd = NewFD(out.node, GetFileFDStat(), false);
  ASSERT(out_fd.fd == 1);

  NewNodeResult err = NewNode(NULL, "stderr", GetCharDeviceStat(kStderrDevice));
  NewFDResult err_fd = NewFD(err.node, GetFileFDStat(), false);
  ASSERT(err_fd.fd == 2);

  NewNodeResult root = NewNode(NULL, "/", GetDirectoryStat());
  NewFDResult root_fd = NewFD(root.node, GetDirectoryFDStat(), true);
  ASSERT(root_fd.fd == 3);
  AddDirent(root.node, ".", 1, root.inode);
  AddDirent(root.node, "..", 2, root.inode);
}

WASM_EXPORT
void init(void) {
  InitInodes();
  CreateStdFds();
}

WASM_EXPORT __wasi_errno_t fd_allocate(__wasi_fd_t fd, __wasi_filesize_t offset,
                                       __wasi_filesize_t len) {
  tracef("!!fd_allocate(fd:%u, offset:%" PRIu64 ", len:%" PRIu64 ")", fd,
         offset, len);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_close(__wasi_fd_t fd) {
  tracef("!!fd_close(fd:%u)", fd);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_fdstat_get(__wasi_fd_t fd, __wasi_fdstat_t *buf) {
  tracef("!!fd_fdstat_get(fd:%u, buf:%p)", fd, buf);
  FDesc* fdesc = GetFDesc(fd);
  if (fdesc == NULL) {
    return __WASI_EBADF;
  }
  copy_out(buf, &fdesc->stat, sizeof(*buf));
  return __WASI_ESUCCESS;
}

WASM_EXPORT __wasi_errno_t fd_fdstat_set_flags(__wasi_fd_t fd,
                                               __wasi_fdflags_t flags) {
  tracef("!!fd_fdstat_set_flags(fd:%u, flags:%u)", fd, flags);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_filestat_get(__wasi_fd_t fd,
                                           __wasi_filestat_t *buf) {
  tracef("!!fd_filestat_get(fd:%u, buf:%p)", fd, buf);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_filestat_set_size(__wasi_fd_t fd,
                                                __wasi_filesize_t st_size) {
  tracef("!!fd_filestat_set_size(fd:%u, buf:%" PRIu64 ")", fd, st_size);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_pread(__wasi_fd_t fd, const __wasi_iovec_t *iovs,
                                    size_t iovs_len, __wasi_filesize_t offset,
                                    size_t *nread) {
  tracef("!!fd_pread(fd:%u, iovs:%p, iovs_len:%zu, offset:%" PRIu64
         ", nread:%p)",
         fd, iovs, iovs_len, offset, nread);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_prestat_dir_name(__wasi_fd_t fd, char *path,
                                               size_t path_len) {
  tracef("!!fd_prestat_dir_name(fd:%u, path:%p, path_len:%zu)", fd, path,
         path_len);
  FDesc* fdesc = GetFDesc(fd);
  if (fdesc == NULL || !fdesc->is_prestat) {
    return __WASI_EBADF;
  }
  ASSERT(fdesc->stat.fs_filetype == __WASI_FILETYPE_DIRECTORY);
  Node* node = GetNode(fdesc->inode);
  size_t len = node->name_len;
  if (len < path_len) {
    len = path_len;
  }
  copy_out(path, &node->name, len);
  return __WASI_ESUCCESS;
}

WASM_EXPORT __wasi_errno_t fd_prestat_get(__wasi_fd_t fd,
                                          __wasi_prestat_t *buf) {
  tracef("!!fd_prestat_get(fd:%u, buf:%p)", fd, buf);
  FDesc* fdesc = GetFDesc(fd);
  if (fdesc == NULL || !fdesc->is_prestat) {
    return __WASI_EBADF;
  }
  ASSERT(fdesc->stat.fs_filetype == __WASI_FILETYPE_DIRECTORY);
  __wasi_prestat_t prestat = {.pr_type = __WASI_PREOPENTYPE_DIR,
                              .u = {{GetNode(fdesc->inode)->name_len + 1}}};
  copy_out(buf, &prestat, sizeof(buf));
  return __WASI_ESUCCESS;
}

WASM_EXPORT __wasi_errno_t fd_read(__wasi_fd_t fd, const __wasi_iovec_t *iovs,
                                   size_t iovs_len, size_t *nread) {
  tracef("!!fd_read(fd:%u, iovs:%p, iovs_len:%zu, nread:%p)", fd, iovs,
         iovs_len, nread);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_readdir(__wasi_fd_t fd, void *buf, size_t buf_len,
                                      __wasi_dircookie_t cookie,
                                      size_t *bufused) {
  tracef("!!fd_readdir(fd:%u, buf:%p, buf_len:%zu, dir_cookie:%" PRIu64
         " bufused:%p)",
         fd, buf, buf_len, cookie, bufused);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_seek(__wasi_fd_t fd, __wasi_filedelta_t offset,
                                   __wasi_whence_t whence,
                                   __wasi_filesize_t *newoffset) {
  tracef("!!fd_seek(fd:%u, offset:%" PRIu64 ", buf_len:%u, newoffset:%p)", fd,
         offset, whence, newoffset);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_write(__wasi_fd_t fd, const __wasi_ciovec_t *iovs,
                                    size_t iovs_len, size_t *nwritten) {
  tracef("!!fd_write(fd:%u, iovs:%p, iovs_len:%zu, nwritten:%p)", fd, iovs,
         iovs_len, nwritten);
  // XXX
  if (fd <= 2) {
    return host_write(fd, iovs, iovs_len, nwritten);
  }
  return __WASI_EBADF;
}

typedef struct {
  Node* node;
  Node* parent;
} LookupResult;

static LookupResult LookupPath(Node *dirnode, const char *path,
                               size_t path_len) {
  ASSERT(dirnode && dirnode->stat.st_filetype == __WASI_FILETYPE_DIRECTORY);

  // Find path component to look up [0, sep).
  size_t sep;
  for (sep = 0; sep < path_len && path[sep] != '/'; ++sep) {
  }

  __wasi_dirent_t* dirent = dirnode->dir.dirents;
  while (dirent) {
    size_t len = dirent->d_namlen;
    if (len == sep && memcmp(path, GetDirentName(dirent), len) == 0) {
      // Match, search next component.
      Node* node = GetNode(dirent->d_ino);
      if (sep == path_len) {
        // End of path.
        return (LookupResult){.node = node, .parent = dirnode};
      } else if (dirent->d_type == __WASI_FILETYPE_DIRECTORY) {
        // Look in next directory.
        ASSERT(sep < path_len);
        return LookupPath(node, path + sep + 1, path_len - sep - 1);
      } else {
        // Not a directory; fail.
        break;
      }
    } else {
      // No match.
      dirent = GetNextDirent(dirnode, dirent);
    }
  }
  // Nothing in this directory with that name, fail.
  return (LookupResult){.node = NULL, .parent = NULL};
}

WASM_EXPORT __wasi_errno_t path_create_directory(__wasi_fd_t fd,
                                                 const char *path,
                                                 size_t path_len) {
  copy_in(g_path_buf, path, path_len);
  tracef("!!path_create_directory(fd:%u, path:\"%.*s\")", fd, (int)path_len,
         g_path_buf);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_filestat_get(__wasi_fd_t fd,
                                             __wasi_lookupflags_t flags,
                                             const char *path, size_t path_len,
                                             __wasi_filestat_t *buf) {
  copy_in(g_path_buf, path, path_len);
  tracef("!!path_filestat_get(fd:%u, flags:%u, path:\"%.*s\", buf:%p)", fd,
         flags, (int)path_len, g_path_buf, buf);
  FDesc* fdesc = GetFDesc(fd);
  if (fdesc == NULL) {
    return __WASI_EBADF;
  }
  LookupResult lookup = LookupPath(GetNode(fdesc->inode), g_path_buf, path_len);
  if (!lookup.node) {
    return __WASI_ENOENT;
  }
  copy_out(buf, &lookup.node->stat, sizeof(*buf));
  return __WASI_ESUCCESS;
}

WASM_EXPORT __wasi_errno_t
path_open(__wasi_fd_t dirfd, __wasi_lookupflags_t dirflags, const char *path,
          size_t path_len, __wasi_oflags_t oflags,
          __wasi_rights_t fs_rights_base, __wasi_rights_t fs_rights_inheriting,
          __wasi_fdflags_t fs_flags, __wasi_fd_t *fd) {
  copy_in(g_path_buf, path, path_len);
  tracef("!!path_open(dirfd:%u, dirflags:%u, path:\"%.*s\", oflags:%u, "
         "fs_rights_base:%" PRIu64 ", fs_rights_inheriting:%" PRIu64
         ", fs_flags:%u, fd:%p)",
         dirfd, dirflags, (int)path_len, g_path_buf, oflags, fs_rights_base,
         fs_rights_inheriting, fs_flags, fd);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_readlink(__wasi_fd_t fd, const char *path,
                                         size_t path_len, char *buf,
                                         size_t buf_len, size_t *bufused) {
  copy_in(g_path_buf, path, path_len);
  tracef("!!path_readlink(fd:%u, path:\"%.*s\", buf:%p, buf_len:%zu, "
         "bufused:%p)",
         fd, (int)path_len, g_path_buf, buf, buf_len, bufused);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_remove_directory(__wasi_fd_t fd,
                                                 const char *path,
                                                 size_t path_len) {
  copy_in(g_path_buf, path, path_len);
  tracef("!!path_remove_directory(fd:%u, path:\"%.*s\")", fd, (int)path_len,
         g_path_buf);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_rename(__wasi_fd_t old_fd, const char *old_path,
                                       size_t old_path_len, __wasi_fd_t new_fd,
                                       const char *new_path,
                                       size_t new_path_len) {
  tracef("!!path_rename(old_fd:%u, old_path:\"%.*s\", new_fd:%u, "
         "new_path:\"%.*s\")",
         old_fd, (int)old_path_len, old_path, new_fd, (int)new_path_len,
         new_path);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_symlink(const char *old_path,
                                        size_t old_path_len, __wasi_fd_t fd,
                                        const char *new_path,
                                        size_t new_path_len) {
  tracef("!!path_symlink(old_path:\"%.*s\", new_path:\"%.*s\")",
         (int)old_path_len, old_path, (int)new_path_len, new_path);
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_unlink_file(__wasi_fd_t fd, const char *path,
                                            size_t path_len) {
  copy_in(g_path_buf, path, path_len);
  tracef("!!path_symlink(fd:%u, path:\"%.*s\")", fd, (int)path_len, g_path_buf);
  return __WASI_ENOTCAPABLE;
}
