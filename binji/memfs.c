#include <stdlib.h>
#include <string.h>
#include <wasi/core.h>

#define WASM_EXPORT __attribute__((__visibility__("default")))
#define MAX_FDS 4096
#define MAX_NODES 1024

extern __wasi_errno_t host_write(__wasi_fd_t fd, const __wasi_ciovec_t *iovs,
                                 size_t iovs_len, size_t *nwritten);
extern void copy_out(void* their_dest, const void* my_src, size_t size);
extern void copy_in(void* my_dest, const void* their_src, size_t size);

typedef struct FD FD;
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
  size_t capacity;
};

struct Node {
  Node* parent;
  char* name;
  __wasi_filestat_t stat;
  union {
    FileContents file;
    DirectoryContents dir;
  };
};

struct FD {
  __wasi_fdstat_t stat;
  Node* node;
};

static Node g_nodes[MAX_NODES];
static FD g_fds[MAX_FDS];
static __wasi_device_t g_next_device = 0;
static __wasi_inode_t g_next_inode = 0;
static __wasi_fd_t g_max_fd = 0;

static __wasi_errno_t nextfd(__wasi_fd_t *fd) {
  for (__wasi_fd_t i = 0; i < MAX_FDS; ++i) {
    if (g_fds[i].stat.fs_filetype == __WASI_FILETYPE_UNKNOWN) {
      *fd = i;
      return __WASI_ESUCCESS;
    }
  }
  return __WASI_EMFILE;
}

WASM_EXPORT
static void init() {
}

static const __wasi_rights_t kDefaultFileRights =
    __WASI_RIGHT_FD_DATASYNC | __WASI_RIGHT_FD_READ | __WASI_RIGHT_FD_SEEK |
    __WASI_RIGHT_FD_FDSTAT_SET_FLAGS | __WASI_RIGHT_FD_SYNC |
    __WASI_RIGHT_FD_TELL | __WASI_RIGHT_FD_WRITE | __WASI_RIGHT_FD_ADVISE |
    __WASI_RIGHT_FD_ALLOCATE;

static const __wasi_rights_t kDefaultDirectoryRights =
    __WASI_RIGHT_PATH_CREATE_DIRECTORY | __WASI_RIGHT_PATH_CREATE_FILE |
    __WASI_RIGHT_PATH_LINK_SOURCE | __WASI_RIGHT_PATH_LINK_TARGET |
    __WASI_RIGHT_PATH_OPEN | __WASI_RIGHT_FD_READDIR |
    __WASI_RIGHT_PATH_READLINK | __WASI_RIGHT_PATH_RENAME_SOURCE |
    __WASI_RIGHT_PATH_RENAME_TARGET | __WASI_RIGHT_PATH_FILESTAT_GET |
    __WASI_RIGHT_PATH_FILESTAT_SET_SIZE | __WASI_RIGHT_PATH_FILESTAT_SET_TIMES |
    __WASI_RIGHT_FD_FILESTAT_GET | __WASI_RIGHT_FD_FILESTAT_SET_SIZE |
    __WASI_RIGHT_FD_FILESTAT_SET_TIMES | __WASI_RIGHT_PATH_SYMLINK |
    __WASI_RIGHT_PATH_REMOVE_DIRECTORY | __WASI_RIGHT_PATH_UNLINK_FILE;

static __wasi_fdstat_t g_fdstats[MAX_FDS] = {
    // STDIN
    {__WASI_FILETYPE_CHARACTER_DEVICE, 0, kDefaultFileRights,
     kDefaultFileRights},
    // STDOUT
    {__WASI_FILETYPE_CHARACTER_DEVICE, __WASI_FDFLAG_APPEND, kDefaultFileRights,
     kDefaultFileRights},
    // STDERR
    {__WASI_FILETYPE_CHARACTER_DEVICE, __WASI_FDFLAG_APPEND, kDefaultFileRights,
     kDefaultFileRights},
    // root
    {__WASI_FILETYPE_DIRECTORY, 0, kDefaultDirectoryRights,
     kDefaultDirectoryRights},
};

static const char *g_prestat_dirnames[] = {
    "/", // ROOT
};
static __wasi_prestat_t g_prestats[] = {
    {__WASI_PREOPENTYPE_DIR, {{1}}}, // ROOT
};
static const size_t g_prestats_count =
    sizeof(g_prestats) / sizeof(g_prestats[0]);

static int is_valid_fd(__wasi_fd_t fd) {
  return fd <= g_max_fd;
}

static int is_valid_prestat_fd(__wasi_fd_t fd) {
  return fd >= 3 && fd < g_prestats_count + 3;
}

WASM_EXPORT __wasi_errno_t fd_allocate(__wasi_fd_t fd, __wasi_filesize_t offset,
                                       __wasi_filesize_t len) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_close(__wasi_fd_t fd) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_fdstat_get(__wasi_fd_t fd, __wasi_fdstat_t *buf) {
  if (!is_valid_fd(fd)) {
    return __WASI_EBADF;
  }
  copy_out(buf, &g_fdstats[fd], sizeof(*buf));
  return __WASI_ESUCCESS;
}

WASM_EXPORT __wasi_errno_t fd_fdstat_set_flags(__wasi_fd_t fd,
                                               __wasi_fdflags_t flags) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_filestat_get(__wasi_fd_t fd,
                                           __wasi_filestat_t *buf) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_filestat_set_size(__wasi_fd_t fd,
                                                __wasi_filesize_t st_size) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_pread(__wasi_fd_t fd, const __wasi_iovec_t *iovs,
                                    size_t iovs_len, __wasi_filesize_t offset,
                                    size_t *nread) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_prestat_dir_name(__wasi_fd_t fd, char *path,
                                               size_t path_len) {
  if (!is_valid_prestat_fd(fd)) {
    return __WASI_EBADF;
  }
  const char* name = g_prestat_dirnames[fd - 3];
  size_t len = strlen(name);
  if (len > path_len) {
    len = path_len;
  }
  copy_out(path, name, len);
  return __WASI_ESUCCESS;
}

WASM_EXPORT __wasi_errno_t fd_prestat_get(__wasi_fd_t fd,
                                          __wasi_prestat_t *buf) {
  if (!is_valid_prestat_fd(fd)) {
    return __WASI_EBADF;
  }
  copy_out(buf, &g_prestats[fd - 3], sizeof(*buf));
  return __WASI_ESUCCESS;
}

WASM_EXPORT __wasi_errno_t fd_read(__wasi_fd_t fd, const __wasi_iovec_t *iovs,
                                   size_t iovs_len, size_t *nread) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_readdir(__wasi_fd_t fd, void *buf, size_t buf_len,
                                      __wasi_dircookie_t cookie,
                                      size_t *bufused) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_seek(__wasi_fd_t fd, __wasi_filedelta_t offset,
                                   __wasi_whence_t whence,
                                   __wasi_filesize_t *newoffset) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t fd_write(__wasi_fd_t fd, const __wasi_ciovec_t *iovs,
                                    size_t iovs_len, size_t *nwritten) {
  // XXX
  if (fd <= 2) {
    return host_write(fd, iovs, iovs_len, nwritten);
  }
  return __WASI_EBADF;
}

WASM_EXPORT __wasi_errno_t path_create_directory(__wasi_fd_t fd,
                                                 const char *path,
                                                 size_t path_len) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_filestat_get(__wasi_fd_t fd,
                                             __wasi_lookupflags_t flags,
                                             const char *path, size_t path_len,
                                             __wasi_filestat_t *buf) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t
path_open(__wasi_fd_t dirfd, __wasi_lookupflags_t dirflags, const char *path,
          size_t path_len, __wasi_oflags_t oflags,
          __wasi_rights_t fs_rights_base, __wasi_rights_t fs_rights_inheriting,
          __wasi_fdflags_t fs_flags, __wasi_fd_t *fd) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_readlink(__wasi_fd_t fd, const char *path,
                                         size_t path_len, char *buf,
                                         size_t buf_len, size_t *bufused) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_remove_directory(__wasi_fd_t fd,
                                                 const char *path,
                                                 size_t path_len) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_rename(__wasi_fd_t old_fd, const char *old_path,
                                       size_t old_path_len, __wasi_fd_t new_fd,
                                       const char *new_path,
                                       size_t new_path_len) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_symlink(const char *old_path,
                                        size_t old_path_len, __wasi_fd_t fd,
                                        const char *new_path,
                                        size_t new_path_len) {
  return __WASI_ENOTCAPABLE;
}

WASM_EXPORT __wasi_errno_t path_unlink_file(__wasi_fd_t fd, const char *path,
                                            size_t path_len) {
  return __WASI_ENOTCAPABLE;
}
