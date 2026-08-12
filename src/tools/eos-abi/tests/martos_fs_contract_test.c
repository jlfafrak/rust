#include "eos_rust_abi.h"

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
#define EOS_ERRNO_NOT_SUPPORTED 45

static int eos_fake_errno;
static int eos_fake_child_exists;
static int eos_fake_recursive_remove_called;
static int eos_fake_file_init_called;
static int eos_fake_file_open_called;
static int eos_fake_file_close_called;
static int eos_fake_delete_after_exists;
static int eos_fake_delete_after_open;
static int eos_fake_rename_errno;
static os_efs_entry_type eos_fake_existing_type;

static os_status eos_fake_entry_exists(const char *path,
                                       os_efs_entry_type *type,
                                       uint32 timeout) {
    (void)path;
    (void)timeout;
    *type = eos_fake_existing_type;
    if (eos_fake_delete_after_exists) {
        eos_fake_existing_type = OS_EFS_NONE;
    }
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
    if (eos_fake_existing_type == OS_EFS_NONE) {
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
    eos_fake_child_exists = 0;
    return OS_STS_OK;
}

static int eos_fake_rmdir(const char *path) {
    (void)path;
    if (eos_fake_child_exists) {
        eos_fake_errno = 66;
        return -1;
    }
    return 0;
}

static int eos_fake_unlink(const char *path) {
    (void)path;
    if (eos_fake_child_exists) {
        eos_fake_errno = 21;
        return -1;
    }
    return 0;
}

static int eos_fake_rename(const char *old_path, const char *new_path) {
    (void)old_path;
    (void)new_path;
    eos_fake_errno = eos_fake_rename_errno;
    return -1;
}

#define errno eos_fake_errno
#define EOS_MARTOS_LIBC_ERRNO_VALUE eos_fake_errno
#define os_efs_entry_exists eos_fake_entry_exists
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
    eos_fake_child_exists = 1;
    eos_port_result result = eos_martos_fs_path_rmdir("/race");
    if (expect(result.error_number == 66 &&
               eos_fake_child_exists == 1 &&
               eos_fake_recursive_remove_called == 0) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }
    result = eos_martos_fs_path_unlink("/replaced-by-directory");
    return expect(result.error_number == 21 &&
                  eos_fake_child_exists == 1 &&
                  eos_fake_recursive_remove_called == 0);
#elif defined(EOS_RUST_MARTOS_FS_CASE_trunc)
    os_efs_file_id file = {0};
    eos_port_result result;
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

    eos_fake_existing_type = OS_EFS_NONE;
    eos_fake_file_init_called = 0;
    eos_fake_file_open_called = 0;
    result = eos_martos_fs_file_open_native(
        "/create", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT, &file);
    if (expect_named(result.status == OS_STS_OK &&
                          eos_fake_file_open_called == 1 &&
                          eos_fake_file_init_called == 1,
                     "O_CREAT must initialize a missing file") !=
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

    eos_fake_existing_type = OS_EFS_FILE;
    eos_fake_file_init_called = 0;
    result = eos_martos_fs_file_open_native(
        "/truncate-create", EOS_RUST_O_WRONLY | EOS_RUST_O_CREAT |
                                EOS_RUST_O_TRUNC,
        &file);
    return expect_named(result.status == OS_STS_OK &&
                            result.error_number == 0 &&
                            eos_fake_file_init_called == 1,
                        "O_CREAT|O_TRUNC must initialize explicitly");
#elif defined(EOS_RUST_MARTOS_FS_CASE_rename)
    static const int native_errno[] = {2, 17, 13, 30};
    static const int32_t expected_status[] = {
        OS_STS_OBJECT_NOT_FOUND, OS_STS_OBJECT_EXISTS,
        OS_STS_INSUFFICIENT_ACL, OS_STS_OBJECT_IS_READ_ONLY};
    size_t index;
    for (index = 0; index < sizeof(native_errno) / sizeof(native_errno[0]);
         ++index) {
        eos_fake_rename_errno = native_errno[index];
        eos_port_result result =
            eos_martos_fs_path_rename("/old", "/new");
        if (result.status != expected_status[index] ||
            result.error_number != native_errno[index]) {
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
#else
#error "one MARTOS filesystem contract case must be selected"
#endif
}
