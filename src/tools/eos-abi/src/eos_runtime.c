#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct eos_environment_snapshot {
    struct eos_environment_snapshot *older;
    uint32_t count;
    char *items[];
} eos_environment_snapshot;

static eos_environment_snapshot *eos_environment_current;
static char *eos_environment_empty[] = {NULL};

static int32_t eos_runtime_lock_acquire(void) {
    return eos_port_lock_acquire(EOS_PORT_LOCK_RUNTIME);
}
static void eos_runtime_lock_release(void) {
    (void)eos_port_lock_release(EOS_PORT_LOCK_RUNTIME);
}

static int eos_environment_name_valid(const char *name) {
    const char *cursor;
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
    for (cursor = name; *cursor != '\0'; ++cursor) {
        if (*cursor == '=') {
            return 0;
        }
    }
    return 1;
}

static uint32_t eos_environment_count(void) {
    return eos_environment_current == NULL ? 0 : eos_environment_current->count;
}

static char **eos_environment_items(void) {
    return eos_environment_current == NULL ? eos_environment_empty
                                           : eos_environment_current->items;
}

static int32_t eos_environment_find(const char *name) {
    const size_t name_length = strlen(name);
    char **items = eos_environment_items();
    uint32_t index;
    for (index = 0; index < eos_environment_count(); ++index) {
        if (strncmp(items[index], name, name_length) == 0 &&
            items[index][name_length] == '=') {
            return (int32_t)index;
        }
    }
    return -1;
}

static char *eos_environment_value_at(int32_t index) {
    char *entry = eos_environment_items()[(uint32_t)index];
    char *separator = strchr(entry, '=');
    return separator + 1;
}

static eos_environment_snapshot *eos_environment_build(const char *name,
                                                        const char *value,
                                                        int32_t existing,
                                                        int remove,
                                                        int32_t *status) {
    const uint32_t old_count = eos_environment_count();
    const uint32_t new_count =
        remove ? old_count - UINT32_C(1)
               : old_count + (existing < 0 ? UINT32_C(1) : UINT32_C(0));
    uint64_t total = (uint64_t)offsetof(eos_environment_snapshot, items) +
                     ((uint64_t)new_count + UINT64_C(1)) * sizeof(char *);
    uint32_t old_index;
    uint32_t new_index = 0;
    eos_environment_snapshot *snapshot;
    char *storage;
    char **old_items = eos_environment_items();

    for (old_index = 0; old_index < old_count; ++old_index) {
        if ((int32_t)old_index != existing) {
            total += (uint64_t)strlen(old_items[old_index]) + UINT64_C(1);
        }
    }
    if (!remove) {
        total += (uint64_t)strlen(name) + UINT64_C(1) +
                 (uint64_t)strlen(value) + UINT64_C(1);
    }
    if (total > UINT32_MAX) {
        *status = EOS_PORT_STATUS_ALLOC_ERROR;
        return NULL;
    }

    *status = eos_port_memory_alloc((uint32_t)total, (void **)&snapshot);
    if (*status != EOS_PORT_STATUS_OK) {
        return NULL;
    }
    snapshot->older = eos_environment_current;
    snapshot->count = new_count;
    storage = (char *)&snapshot->items[new_count + UINT32_C(1)];

    for (old_index = 0; old_index < old_count; ++old_index) {
        size_t length;
        if ((int32_t)old_index == existing) {
            continue;
        }
        length = strlen(old_items[old_index]) + 1U;
        snapshot->items[new_index++] = storage;
        (void)memcpy(storage, old_items[old_index], length);
        storage += length;
    }
    if (!remove) {
        const size_t name_length = strlen(name);
        const size_t value_length = strlen(value);
        snapshot->items[new_index++] = storage;
        (void)memcpy(storage, name, name_length);
        storage[name_length] = '=';
        (void)memcpy(storage + name_length + 1U, value, value_length + 1U);
    }
    snapshot->items[new_index] = NULL;
    return snapshot;
}

static int32_t eos_runtime_fail_status(int32_t status,
                                       const char *operation) {
    const eos_error_result error =
        eos_error_from_port_status_impl(status, operation);
    *eos_port_errno_location() =
        error.kind == EOS_ERROR_ERRNO ? error.error_number : EOS_ERRNO_IO;
    return -1;
}

EOS_RUST_NORETURN void eos_rust_abort(void) {
    abort();
}

EOS_RUST_NORETURN void eos_rust_exit(int32_t status) {
    exit((int)status);
}

char *eos_rust_getenv(const char *name) {
    int32_t index;
    int32_t status;
    char native_value[EOS_PORT_ENV_VALUE_CAPACITY];
    eos_environment_snapshot *snapshot;
    char *result = NULL;

    if (!eos_environment_name_valid(name)) {
        *eos_port_errno_location() = EOS_ERRNO_INVALID;
        return NULL;
    }
    status = eos_runtime_lock_acquire();
    if (status != EOS_PORT_STATUS_OK)
        return (void)eos_runtime_fail_status(status, "runtime.lock"), NULL;
    index = eos_environment_find(name);
    if (index >= 0) {
        result = eos_environment_value_at(index);
        eos_runtime_lock_release();
        return result;
    }

    status = eos_port_environment_get(name,
                                      native_value,
                                      EOS_PORT_ENV_VALUE_CAPACITY);
    if (status == EOS_PORT_STATUS_OK) {
        native_value[EOS_PORT_ENV_VALUE_CAPACITY - UINT32_C(1)] = '\0';
        snapshot = eos_environment_build(name,
                                         native_value,
                                         -1,
                                         0,
                                         &status);
        if (snapshot != NULL) {
            eos_environment_current = snapshot;
            result = eos_environment_value_at(
                (int32_t)(snapshot->count - UINT32_C(1)));
        } else {
            (void)eos_runtime_fail_status(status, "environment.snapshot");
        }
    } else if (status != EOS_PORT_STATUS_OBJECT_NOT_FOUND) {
        (void)eos_runtime_fail_status(status, "environment.get");
    }
    eos_runtime_lock_release();
    return result;
}

int32_t eos_rust_setenv(const char *name,
                        const char *value,
                        int32_t overwrite) {
    int32_t index;
    int32_t status;
    char native_value[EOS_PORT_ENV_VALUE_CAPACITY];
    eos_environment_snapshot *snapshot;

    if (!eos_environment_name_valid(name) || value == NULL ||
        strlen(value) >= EOS_PORT_ENV_VALUE_CAPACITY) {
        *eos_port_errno_location() = EOS_ERRNO_INVALID;
        return -1;
    }
    status = eos_runtime_lock_acquire();
    if (status != EOS_PORT_STATUS_OK)
        return eos_runtime_fail_status(status, "runtime.lock");
    index = eos_environment_find(name);
    if (index >= 0 && strcmp(eos_environment_value_at(index), value) == 0) {
        eos_runtime_lock_release();
        return 0;
    }
    if (index >= 0 && overwrite == 0) {
        eos_runtime_lock_release();
        return 0;
    }
    if (index < 0 && overwrite == 0) {
        status = eos_port_environment_get(name,
                                          native_value,
                                          EOS_PORT_ENV_VALUE_CAPACITY);
        if (status == EOS_PORT_STATUS_OK) {
            native_value[EOS_PORT_ENV_VALUE_CAPACITY - UINT32_C(1)] = '\0';
            snapshot = eos_environment_build(name,
                                             native_value,
                                             -1,
                                             0,
                                             &status);
            if (snapshot == NULL) {
                eos_runtime_lock_release();
                return eos_runtime_fail_status(status,
                                               "environment.snapshot");
            }
            eos_environment_current = snapshot;
            eos_runtime_lock_release();
            return 0;
        }
        if (status != EOS_PORT_STATUS_OBJECT_NOT_FOUND) {
            eos_runtime_lock_release();
            return eos_runtime_fail_status(status, "environment.get");
        }
    }

    snapshot = eos_environment_build(name, value, index, 0, &status);
    if (snapshot == NULL) {
        eos_runtime_lock_release();
        return eos_runtime_fail_status(status, "environment.snapshot");
    }
    status = eos_port_environment_set(name, value);
    if (status != EOS_PORT_STATUS_OK) {
        (void)eos_port_memory_free(snapshot);
        eos_runtime_lock_release();
        return eos_runtime_fail_status(status, "environment.set");
    }
    eos_environment_current = snapshot;
    eos_runtime_lock_release();
    return 0;
}

int32_t eos_rust_unsetenv(const char *name) {
    int32_t index;
    int32_t status;
    eos_environment_snapshot *snapshot = NULL;

    if (!eos_environment_name_valid(name)) {
        *eos_port_errno_location() = EOS_ERRNO_INVALID;
        return -1;
    }
    status = eos_runtime_lock_acquire();
    if (status != EOS_PORT_STATUS_OK)
        return eos_runtime_fail_status(status, "runtime.lock");
    index = eos_environment_find(name);
    if (index >= 0) {
        snapshot = eos_environment_build(name, NULL, index, 1, &status);
        if (snapshot == NULL) {
            eos_runtime_lock_release();
            return eos_runtime_fail_status(status, "environment.snapshot");
        }
    }
    status = eos_port_environment_unset(name);
    if (status != EOS_PORT_STATUS_OK &&
        status != EOS_PORT_STATUS_OBJECT_NOT_FOUND) {
        if (snapshot != NULL) {
            (void)eos_port_memory_free(snapshot);
        }
        eos_runtime_lock_release();
        return eos_runtime_fail_status(status, "environment.unset");
    }
    if (snapshot != NULL) {
        eos_environment_current = snapshot;
    }
    eos_runtime_lock_release();
    return 0;
}

char **eos_rust_environ(void) {
    char **snapshot;
    int32_t status = eos_runtime_lock_acquire();
    if (status != EOS_PORT_STATUS_OK) {
        (void)eos_runtime_fail_status(status, "runtime.lock");
        return eos_environment_empty;
    }
    snapshot = eos_environment_items();
    eos_runtime_lock_release();
    return snapshot;
}
