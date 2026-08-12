#include "eos_rust_abi.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int32_t os_status;
typedef uint32_t uint32;
typedef enum eos_fake_access_mode {
    OS_EFS_READ = 0,
    OS_EFS_WRITE = 1,
    OS_EFS_READ_WRITE = 2,
} os_efs_access_mode;
typedef enum eos_fake_entry_type {
    OS_EFS_NONE = 0,
    OS_EFS_FILE = 1,
    OS_EFS_DIRECTORY = 2,
} os_efs_entry_type;
typedef struct eos_fake_file_id {
    uintptr_t value;
} os_efs_file_id;
typedef struct eos_port_result {
    int32_t status;
    int32_t error_number;
} eos_port_result;

#define OS_STS_OK INT32_C(0)
#define OS_STS_INVALID_PARAM1 INT32_C(1)
#define OS_STS_INVALID_PARAM2 INT32_C(2)
#define OS_STS_INVALID_OBJECT_TYPE INT32_C(11)
#define OS_STS_OBJECT_NOT_FOUND INT32_C(12)
#define OS_STS_OBJECT_EXISTS INT32_C(13)
#define OS_STS_NOT_CALLABLE_FROM_ISR INT32_C(14)
#define OS_STS_INSUFFICIENT_ACL INT32_C(16)
#define OS_STS_OBJECT_IN_USE INT32_C(17)
#define OS_STS_OBJECT_IS_READ_ONLY INT32_C(18)
#define OS_STS_ALLOC_ERROR INT32_C(15)
#define OS_STS_DEVICE_ERROR INT32_C(25)
#define OS_WAIT_FOREVER UINT32_MAX
#define ENOENT 2
#define ENOMEM 12
#define EACCES 13
#define EBUSY 16
#define EEXIST 17
#define EISDIR 21
#define EINVAL 22
#define EROFS 30
#define ENOTEMPTY 66
#define EOS_ERRNO_IO 5
#define EOS_ERRNO_NO_ENTRY 2
#define EOS_ERRNO_NOT_DIRECTORY 20
#define EOS_ERRNO_IS_DIRECTORY 21
#define EOS_ERRNO_NOT_SUPPORTED 45
#define EOS_ERRNO_NAME_TOO_LONG 63
#define EOS_ERRNO_NOT_EMPTY 66

static volatile int eos_fake_global_errno;
static int eos_fake_libc_failure;
static int eos_fake_libc_clobber;
static uint32 eos_fake_directory_count;
static int eos_fake_recursive_remove_called;
static int eos_fake_file_init_called;
static int eos_fake_file_open_called;
static int eos_fake_file_close_called;
#if defined(EOS_RUST_MARTOS_FS_CASE_trunc)
static int eos_fake_delete_after_exists;
static os_status eos_fake_file_open_forced_status;
#endif
static int eos_fake_delete_after_open;
static int eos_fake_missing_becomes_existing;
static int eos_fake_file_contents;
static os_efs_entry_type eos_fake_existing_type;

static os_status eos_fake_entry_exists(const char *path,
                                       os_efs_entry_type *type,
                                       uint32 timeout) {
    (void)timeout;
#if defined(EOS_RUST_MARTOS_FS_CASE_rmdir)
    if (strcmp(path, "/missing") == 0) {
        *type = OS_EFS_NONE;
    } else if (strcmp(path, "/directory") == 0) {
        *type = OS_EFS_DIRECTORY;
    } else {
        *type = OS_EFS_FILE;
    }
#elif defined(EOS_RUST_MARTOS_FS_CASE_rename)
    if (strcmp(path, "/missing") == 0) {
        *type = OS_EFS_NONE;
    } else if (strcmp(path, "/component") == 0) {
        *type = OS_EFS_FILE;
    } else if (strcmp(path, "/source-parent") == 0 ||
               strcmp(path, "/target-parent") == 0) {
        *type = OS_EFS_DIRECTORY;
    } else if (strcmp(path, "/source-parent/source") == 0) {
        *type = OS_EFS_FILE;
    } else {
        *type = OS_EFS_NONE;
    }
#else
    (void)path;
    *type = eos_fake_existing_type;
    if (eos_fake_delete_after_exists) {
        eos_fake_existing_type = OS_EFS_NONE;
    }
#endif
    return OS_STS_OK;
}

static os_status eos_fake_directory_get_listing_count(const char *path,
                                                       bool recursive,
                                                       uint32 *entry_count,
                                                       uint32 timeout) {
    (void)path;
    (void)recursive;
    (void)timeout;
    *entry_count = eos_fake_directory_count;
    return OS_STS_OK;
}

static os_status eos_fake_file_init(const char *path,
                                    os_efs_access_mode mode,
                                    os_efs_file_id *file,
                                    uint32 timeout) {
    (void)path;
    (void)mode;
    (void)timeout;
    ++eos_fake_file_init_called;
    eos_fake_file_contents = 0;
    file->value = UINT32_C(9);
    return OS_STS_OK;
}

static os_status eos_fake_file_open(const char *path,
                                    os_efs_access_mode mode,
                                    os_efs_file_id *file,
                                    uint32 timeout) {
    (void)path;
    (void)mode;
    (void)timeout;
    ++eos_fake_file_open_called;
#if defined(EOS_RUST_MARTOS_FS_CASE_trunc)
    if (eos_fake_file_open_forced_status != OS_STS_OK) {
        return eos_fake_file_open_forced_status;
    }
#endif
    if (eos_fake_existing_type == OS_EFS_NONE) {
        if (eos_fake_missing_becomes_existing) {
            eos_fake_existing_type = OS_EFS_FILE;
            eos_fake_file_contents = 77;
        }
        return OS_STS_OBJECT_NOT_FOUND;
    }
    file->value = UINT32_C(7);
    if (eos_fake_delete_after_open) {
        eos_fake_existing_type = OS_EFS_NONE;
    }
    return OS_STS_OK;
}

static os_status eos_fake_file_close(os_efs_file_id file, uint32 timeout) {
    (void)file;
    (void)timeout;
    ++eos_fake_file_close_called;
    return OS_STS_OK;
}

os_status eos_fake_recursive_remove(const char *path, uint32 timeout) {
    (void)path;
    (void)timeout;
    ++eos_fake_recursive_remove_called;
    return OS_STS_OK;
}

static int eos_fake_rmdir(const char *path) {
    (void)path;
    eos_fake_global_errno = eos_fake_libc_clobber;
    return eos_fake_libc_failure ? -1 : 0;
}

static int eos_fake_unlink(const char *path) {
    (void)path;
    eos_fake_global_errno = eos_fake_libc_clobber;
    return eos_fake_libc_failure ? -1 : 0;
}

static int eos_fake_rename(const char *old_path, const char *new_path) {
    (void)old_path;
    (void)new_path;
    eos_fake_global_errno = eos_fake_libc_clobber;
    return eos_fake_libc_failure ? -1 : 0;
}

#define os_efs_entry_exists eos_fake_entry_exists
#define os_efs_directory_get_listing_count \
    eos_fake_directory_get_listing_count
#define os_efs_file_init eos_fake_file_init
#define os_efs_file_open eos_fake_file_open
#define os_efs_file_close eos_fake_file_close
#define rmdir eos_fake_rmdir
#define unlink eos_fake_unlink
#define rename eos_fake_rename
#include "eos_port_martos_fs_contract.h"

static int expect(int condition) { return condition ? EXIT_SUCCESS : EXIT_FAILURE; }
static inline int expect_named(int condition, const char *name) {
    if (!condition) (void)fprintf(stderr, "%s\n", name);
    return expect(condition);
}

int main(void) {
#if defined(EOS_RUST_MARTOS_FS_CASE_rmdir)
    eos_port_result result = eos_martos_fs_path_unlink("/missing");
    if (expect_named(result.error_number == EOS_ERRNO_NO_ENTRY,
                     "unlink must preflight a missing path") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    result = eos_martos_fs_path_unlink("/directory");
    if (expect_named(result.error_number == EOS_ERRNO_IS_DIRECTORY,
                     "unlink must reject a known directory") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    result = eos_martos_fs_path_unlink("/component/child");
    if (expect_named(result.error_number == EOS_ERRNO_NOT_DIRECTORY,
                     "unlink must identify a non-directory path component") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_libc_failure = 1;
    eos_fake_libc_clobber = ENOENT;
    result = eos_martos_fs_path_unlink("/file");
    if (expect_named(result.error_number == EOS_ERRNO_IO,
                     "unlink failure must ignore global errno") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_libc_clobber = EROFS;
    result = eos_martos_fs_path_unlink("/file");
    if (expect_named(result.error_number == EOS_ERRNO_IO,
                     "unlink result must be invariant under errno clobber") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    eos_fake_libc_failure = 0;
    result = eos_martos_fs_path_rmdir("/missing");
    if (expect_named(result.error_number == EOS_ERRNO_NO_ENTRY,
                     "rmdir must preflight a missing path") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    result = eos_martos_fs_path_rmdir("/file");
    if (expect_named(result.error_number == EOS_ERRNO_NOT_DIRECTORY,
                     "rmdir must reject a known non-directory") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    result = eos_martos_fs_path_rmdir("/component/child");
    if (expect_named(result.error_number == EOS_ERRNO_NOT_DIRECTORY,
                     "rmdir must identify a non-directory path component") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_directory_count = 1;
    result = eos_martos_fs_path_rmdir("/directory");
    if (expect_named(result.error_number == EOS_ERRNO_NOT_EMPTY,
                     "rmdir must identify a known nonempty directory") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_directory_count = 0;
    eos_fake_libc_failure = 1;
    eos_fake_libc_clobber = ENOTEMPTY;
    result = eos_martos_fs_path_rmdir("/directory");
    return expect_named(result.error_number == EOS_ERRNO_IO &&
                            eos_fake_recursive_remove_called == 0,
                        "rmdir race failure must be nonrecursive EIO");
#elif defined(EOS_RUST_MARTOS_FS_CASE_trunc)
    os_efs_file_id file = {0};
    eos_port_result result;
    eos_fake_existing_type = OS_EFS_FILE;
    result = eos_martos_fs_file_open_native(
        "/plain-existing", EOS_RUST_O_RDONLY, &file);
    if (expect_named(result.status == OS_STS_OK &&
                          result.error_number == 0 &&
                          eos_fake_file_init_called == 0,
                     "plain open must open an existing file") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_existing_type = OS_EFS_NONE;
    result = eos_martos_fs_file_open_native(
        "/plain-missing", EOS_RUST_O_RDONLY, &file);
    if (expect_named(result.status == OS_STS_OBJECT_NOT_FOUND &&
                          result.error_number == 0 &&
                          eos_fake_file_init_called == 0,
                     "plain open must preserve a missing path") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_existing_type = OS_EFS_NONE;
    result = eos_martos_fs_file_open_native(
        "/missing", EOS_RUST_O_WRONLY | EOS_RUST_O_TRUNC, &file);
    if (expect_named(result.status == OS_STS_OBJECT_NOT_FOUND &&
                          result.error_number == 0 &&
                          eos_fake_file_init_called == 0,
                     "missing non-CREAT trunc must preserve ENOENT") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_existing_type = OS_EFS_FILE;
    eos_fake_delete_after_exists = 1;
    eos_fake_delete_after_open = 1;
    result = eos_martos_fs_file_open_native(
        "/raced", EOS_RUST_O_WRONLY | EOS_RUST_O_TRUNC, &file);
    if (expect_named(result.error_number == EOS_ERRNO_NOT_SUPPORTED &&
                          eos_fake_existing_type == OS_EFS_NONE &&
                          eos_fake_file_init_called == 0 &&
                          eos_fake_file_close_called == 1,
                     "raced non-CREAT trunc must not initialize") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    eos_fake_delete_after_exists = 0;
    eos_fake_delete_after_open = 0;
    eos_fake_file_init_called = 0;
    eos_fake_file_open_called = 0;
    eos_fake_file_close_called = 0;
    eos_fake_existing_type = OS_EFS_FILE;
    result = eos_martos_fs_file_open_native(
        "/existing", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT, &file);
    if (expect_named(result.status == OS_STS_OK &&
                          result.error_number == 0 &&
                          eos_fake_file_open_called == 1 &&
                          eos_fake_file_init_called == 0,
                     "O_CREAT must open an existing file without init") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_file_open_forced_status = OS_STS_INSUFFICIENT_ACL;
    result = eos_martos_fs_file_open_native(
        "/denied", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT, &file);
    if (expect_named(result.status == OS_STS_INSUFFICIENT_ACL &&
                          result.error_number == 0 &&
                          eos_fake_file_init_called == 0,
                     "O_CREAT must preserve an ordinary open ACL failure") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_file_open_forced_status = OS_STS_OBJECT_IS_READ_ONLY;
    result = eos_martos_fs_file_open_native(
        "/read-only", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT, &file);
    if (expect_named(result.status == OS_STS_OBJECT_IS_READ_ONLY &&
                          result.error_number == 0 &&
                          eos_fake_file_init_called == 0,
                     "O_CREAT must preserve an ordinary open read-only failure") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_file_open_forced_status = OS_STS_OK;

    eos_fake_existing_type = OS_EFS_NONE;
    eos_fake_missing_becomes_existing = 1;
    eos_fake_file_contents = 11;
    eos_fake_file_init_called = 0;
    eos_fake_file_open_called = 0;
    result = eos_martos_fs_file_open_native(
        "/create", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT, &file);
    if (expect_named(result.error_number == EOS_ERRNO_NOT_SUPPORTED &&
                          eos_fake_file_open_called == 1 &&
                          eos_fake_file_init_called == 0 &&
                          eos_fake_existing_type == OS_EFS_FILE &&
                          eos_fake_file_contents == 77,
                     "raced O_CREAT must not initialize a newly-existing file") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_missing_becomes_existing = 0;
    eos_fake_existing_type = OS_EFS_NONE;
    eos_fake_file_init_called = 0;
    result = eos_martos_fs_file_open_native(
        "/missing-create", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT, &file);
    if (expect_named(result.error_number == EOS_ERRNO_NOT_SUPPORTED &&
                          eos_fake_file_init_called == 0,
                     "ordinary O_CREAT cannot safely create on MARTOS") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    eos_fake_existing_type = OS_EFS_FILE;
    eos_fake_file_init_called = 0;
    result = eos_martos_fs_file_open_native(
        "/exclusive", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT |
                          EOS_RUST_O_EXCL,
        &file);
    if (expect_named(result.status == OS_STS_OBJECT_EXISTS &&
                          eos_fake_file_init_called == 0,
                     "O_CREAT|O_EXCL must reject an existing file") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_existing_type = OS_EFS_NONE;
    result = eos_martos_fs_file_open_native(
        "/exclusive-missing", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT |
                                  EOS_RUST_O_EXCL,
        &file);
    if (expect_named(result.error_number == EOS_ERRNO_NOT_SUPPORTED &&
                          eos_fake_file_init_called == 0,
                     "O_CREAT|O_EXCL must not emulate atomic creation") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    result = eos_martos_fs_file_open_native(
        "/exclusive-trunc-missing", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT |
                                        EOS_RUST_O_EXCL | EOS_RUST_O_TRUNC,
        &file);
    if (expect_named(result.error_number == EOS_ERRNO_NOT_SUPPORTED &&
                          eos_fake_file_init_called == 0,
                     "O_EXCL must also block create-truncate emulation") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    eos_fake_existing_type = OS_EFS_FILE;
    eos_fake_file_init_called = 0;
    result = eos_martos_fs_file_open_native(
        "/truncate-create", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT |
                                EOS_RUST_O_TRUNC,
        &file);
    if (expect_named(result.status == OS_STS_OK &&
                         result.error_number == 0 &&
                         eos_fake_file_init_called == 1,
                     "O_CREAT|O_TRUNC must initialize explicitly") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_existing_type = OS_EFS_NONE;
    eos_fake_file_init_called = 0;
    result = eos_martos_fs_file_open_native(
        "/truncate-create-missing", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT |
                                        EOS_RUST_O_TRUNC,
        &file);
    return expect_named(result.status == OS_STS_OK &&
                            result.error_number == 0 &&
                            eos_fake_file_init_called == 1,
                        "O_CREAT|O_TRUNC may create explicitly");
#elif defined(EOS_RUST_MARTOS_FS_CASE_rename)
    eos_port_result result =
        eos_martos_fs_path_rename("/missing", "/target-parent/new");
    if (expect_named(result.error_number == EOS_ERRNO_NO_ENTRY,
                     "rename must preflight a missing source") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    result = eos_martos_fs_path_rename(
        "/component/old", "/target-parent/new");
    if (expect_named(result.error_number == EOS_ERRNO_NOT_DIRECTORY,
                     "rename must identify a non-directory path component") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    result = eos_martos_fs_path_rename(
        "/source-parent/source", "/component/new");
    if (expect_named(result.error_number == EOS_ERRNO_NOT_DIRECTORY,
                     "rename must identify a non-directory destination component") !=
        EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_libc_failure = 1;
    eos_fake_libc_clobber = ENOENT;
    result = eos_martos_fs_path_rename(
        "/source-parent/source", "/target-parent/new");
    if (expect_named(result.error_number == EOS_ERRNO_IO,
                     "rename failure must ignore global errno") != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    eos_fake_libc_clobber = EROFS;
    result = eos_martos_fs_path_rename(
        "/source-parent/source", "/target-parent/new");
    return expect_named(result.error_number == EOS_ERRNO_IO,
                        "rename result must be invariant under errno clobber");
#else
#error "one MARTOS filesystem contract case must be selected"
#endif
}
