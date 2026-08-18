#ifndef EOS_PORT_MARTOS_PROCESS_CONTRACT_H
#define EOS_PORT_MARTOS_PROCESS_CONTRACT_H

#include <stdint.h>

typedef int32_t (*eos_martos_process_get_name)(uintptr_t thread, char *name,
                                                uint32_t capacity,
                                                void *context);
typedef int32_t (*eos_martos_process_delete)(uintptr_t thread, void *context);
typedef int32_t (*eos_martos_process_get_count)(uint32_t *count,
                                                void *context);
typedef int32_t (*eos_martos_process_allocate)(uint32_t bytes, void **memory,
                                               void *context);
typedef int32_t (*eos_martos_process_snapshot)(
    void *status_storage, uint32_t capacity, uintptr_t *threads,
    uint32_t *count, void *context);
typedef int32_t (*eos_martos_process_release)(void *memory, void *context);
typedef void (*eos_martos_process_fail_fast)(void *context);

static int eos_martos_process_options_supported(uint32_t environment_count,
                                                uint32_t has_cwd,
                                                uint32_t redirects_stderr,
                                                uint32_t inherited_count) {
    return environment_count == UINT32_C(0) && has_cwd == UINT32_C(0) &&
           redirects_stderr == UINT32_C(0) && inherited_count == UINT32_C(0);
}

static int32_t eos_martos_process_read_result(int32_t count,
                                              int32_t status_ok,
                                              int32_t status_eof,
                                              int32_t status_error) {
    return count == 1 ? status_ok : (count == 0 ? status_eof : status_error);
}

static int32_t eos_martos_process_write_result(int32_t count,
                                               int32_t status_ok,
                                               int32_t status_error) {
    return count == 1 ? status_ok : status_error;
}

static int eos_martos_process_name_equal(const char *left,
                                         const char *right,
                                         uint32_t capacity) {
    uint32_t index;
    for (index = 0; index < capacity; ++index) {
        if (left[index] != right[index]) return 0;
        if (left[index] == '\0') return 1;
    }
    return 0;
}

static int32_t eos_martos_process_kill_matching(
    const uintptr_t *threads, uint32_t count, const char *expected_name,
    uint32_t name_capacity, int32_t status_ok, int32_t status_not_found,
    eos_martos_process_get_name get_name,
    eos_martos_process_delete delete_thread, void *context,
    uint32_t *matched) {
    char name[64];
    uint32_t index;
    int32_t status;
    if (threads == NULL || expected_name == NULL || get_name == NULL ||
        delete_thread == NULL || matched == NULL || name_capacity == 0 ||
        name_capacity > (uint32_t)sizeof(name)) {
        return INT32_C(1);
    }
    *matched = UINT32_C(0);
    for (index = 0; index < count; ++index) {
        name[0] = '\0';
        status = get_name(threads[index], name, name_capacity, context);
        if (status == status_not_found) continue;
        if (status != status_ok) return status;
        name[name_capacity - UINT32_C(1)] = '\0';
        if (!eos_martos_process_name_equal(name, expected_name,
                                           name_capacity)) {
            continue;
        }
        status = delete_thread(threads[index], context);
        if (status == status_not_found) continue;
        if (status != status_ok) return status;
        ++*matched;
    }
    return status_ok;
}

static int32_t eos_martos_process_enumerate_and_kill(
    const char *expected_name, uint32_t name_capacity,
    uint32_t status_item_size, int32_t status_ok,
    int32_t status_not_found, int32_t status_alloc_error,
    eos_martos_process_get_count get_count,
    eos_martos_process_allocate allocate,
    eos_martos_process_snapshot snapshot,
    eos_martos_process_release release,
    eos_martos_process_get_name get_name,
    eos_martos_process_delete delete_thread,
    eos_martos_process_fail_fast fail_fast,
    void *context, uint32_t *matched) {
    void *status_storage = NULL;
    uintptr_t *threads = NULL;
    uint32_t capacity = 0;
    uint32_t count = 0;
    int32_t status;
    int32_t cleanup;
    if (matched == NULL || get_count == NULL || allocate == NULL ||
        snapshot == NULL || release == NULL || fail_fast == NULL ||
        status_item_size == 0) {
        return INT32_C(1);
    }
    *matched = UINT32_C(0);
    status = get_count(&capacity, context);
    if (status != status_ok || capacity == 0) return status;
    if (capacity > UINT32_MAX / status_item_size ||
        capacity > UINT32_MAX / (uint32_t)sizeof(*threads)) {
        return status_alloc_error;
    }
    status = allocate(capacity * status_item_size, &status_storage, context);
    if (status != status_ok || status_storage == NULL) {
        return status == status_ok ? status_alloc_error : status;
    }
    status = allocate(capacity * (uint32_t)sizeof(*threads),
                      (void **)&threads, context);
    if (status != status_ok || threads == NULL) {
        cleanup = release(status_storage, context);
        if (cleanup != status_ok) fail_fast(context);
        return status == status_ok ? status_alloc_error : status;
    }
    status = snapshot(status_storage, capacity, threads, &count, context);
    if (status == status_ok) {
        if (count > capacity) {
            status = INT32_C(25);
        } else {
            status = eos_martos_process_kill_matching(
                threads, count, expected_name, name_capacity, status_ok,
                status_not_found, get_name, delete_thread, context, matched);
        }
    }
    cleanup = release(threads, context);
    if (cleanup != status_ok) fail_fast(context);
    cleanup = release(status_storage, context);
    if (cleanup != status_ok) fail_fast(context);
    return status;
}

static int eos_martos_process_cleanup_failed(int32_t status,
                                             int32_t status_ok) {
    return status != status_ok;
}

#endif
