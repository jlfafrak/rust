#include "eos_process.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#define EOS_PROCESS_EVENT_LOADED UINT32_C(1)
#define EOS_PROCESS_EVENT_COMPLETED UINT32_C(2)
#define EOS_PROCESS_WORKER_STACK UINT32_C(4096)

typedef struct eos_process_record {
    eos_rust_process_t identity;
    uint32_t references;
    uint32_t in_registry;
    _Atomic uint32_t completed;
    uint32_t loaded;
    uint32_t pins_released;
    uint32_t kill_delivered;
    int32_t load_status;
    int32_t run_status;
    int32_t exit_code;
    eos_port_sync completion;
    eos_port_process_request request;
    eos_fd_process_pin stdio[3];
    eos_fd_process_pin inherited_pins[EOS_FD_TABLE_CAPACITY];
    eos_port_process_inherited inherited[EOS_FD_TABLE_CAPACITY];
    uint32_t inherited_count;
    char *program;
    char **argv;
    char **envp;
    char *cwd;
    char name[32];
    struct eos_process_record *next;
} eos_process_record;

static eos_process_record *eos_process_records;
static uint32_t eos_process_next_identity = UINT32_C(1);
static uint32_t eos_process_identity_exhausted;
#ifdef EOS_RUST_HOST_TEST
static _Atomic uint32_t eos_process_live_record_count;
#endif

_Static_assert(sizeof(eos_rust_process_t) == 4,
               "process identity must remain 32-bit");
_Static_assert(sizeof(eos_rust_process_status) == 32,
               "process status ABI size changed");
_Static_assert(_Alignof(eos_rust_process_status) == _Alignof(uint32_t),
               "process status ABI alignment changed");
_Static_assert(offsetof(eos_rust_process_status, code) == 4,
               "process status code offset changed");

static int32_t eos_process_fail_errno(int32_t error_number) {
    *eos_tls_errno_location() = error_number;
    return -1;
}

static int32_t eos_process_mapped_status(int32_t status,
                                         const char *operation) {
    eos_error_result mapped = eos_error_from_port_status_impl(status, operation);
    return mapped.kind == EOS_ERROR_ERRNO ? mapped.error_number : EOS_ERRNO_IO;
}

static int32_t eos_process_fail_status(int32_t status,
                                       const char *operation) {
    return eos_process_fail_errno(eos_process_mapped_status(status, operation));
}

static int32_t eos_process_registry_lock(void) {
    int32_t status = eos_port_lock_acquire(EOS_PORT_LOCK_PROCESS_REGISTRY);
    return status == EOS_PORT_STATUS_OK
               ? 0 : eos_process_fail_status(status, "process.registry.lock");
}

static void eos_process_registry_unlock(void) {
    if (eos_port_lock_release(EOS_PORT_LOCK_PROCESS_REGISTRY) !=
        EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
}

static eos_process_record *eos_process_find_locked(eos_rust_process_t identity) {
    eos_process_record *record = eos_process_records;
    while (record != NULL && record->identity != identity) record = record->next;
    return record;
}

static int eos_process_unref_locked(eos_process_record *record) {
    if (record->references == 0) eos_rust_abort();
    --record->references;
    return record->references == 0;
}

static int eos_process_drop_registry_locked(eos_process_record *record) {
    eos_process_record **link = &eos_process_records;
    if (record->in_registry == 0) return 0;
    while (*link != NULL && *link != record) link = &(*link)->next;
    if (*link != record) eos_rust_abort();
    *link = record->next;
    record->in_registry = UINT32_C(0);
    return eos_process_unref_locked(record);
}

static void eos_process_release_pins(eos_process_record *record) {
    uint32_t index;
    if (record->pins_released != UINT32_C(0)) return;
    for (index = 0; index < UINT32_C(3); ++index) {
        eos_fd_process_unpin(&record->stdio[index]);
    }
    for (index = 0; index < record->inherited_count; ++index) {
        eos_fd_process_unpin(&record->inherited_pins[index]);
    }
    record->pins_released = UINT32_C(1);
}

static void eos_process_free_string_array(char **array, uint32_t count) {
    uint32_t index;
    if (array == NULL) return;
    for (index = 0; index < count; ++index) {
        if (array[index] != NULL && eos_port_memory_free(array[index]) !=
                                      EOS_PORT_STATUS_OK) {
            eos_rust_abort();
        }
    }
    if (eos_port_memory_free(array) != EOS_PORT_STATUS_OK) eos_rust_abort();
}

static void eos_process_destroy(eos_process_record *record) {
    eos_process_release_pins(record);
    eos_process_free_string_array(record->argv, record->request.argc);
    eos_process_free_string_array(record->envp, record->request.envc);
    if (record->cwd != NULL && eos_port_memory_free(record->cwd) !=
                                   EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    if (record->program != NULL && eos_port_memory_free(record->program) !=
                                       EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    if (eos_port_sync_destroy(record->completion) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    if (eos_port_memory_free(record) != EOS_PORT_STATUS_OK) eos_rust_abort();
#ifdef EOS_RUST_HOST_TEST
    (void)atomic_fetch_sub_explicit(&eos_process_live_record_count,
                                    UINT32_C(1), memory_order_release);
#endif
}

static int32_t eos_process_copy_string(const char *source, char **destination) {
    size_t length;
    int32_t status;
    char *copy = NULL;
    if (source == NULL || destination == NULL) return EOS_ERRNO_FAULT;
    length = strlen(source);
    if (length >= (size_t)UINT32_MAX) return EOS_ERRNO_NAME_TOO_LONG;
    status = eos_port_memory_alloc((uint32_t)length + UINT32_C(1),
                                   (void **)&copy);
    if (status != EOS_PORT_STATUS_OK) {
        return eos_process_mapped_status(status, "process.string.allocate");
    }
    (void)memcpy(copy, source, length + 1U);
    *destination = copy;
    return 0;
}

static int32_t eos_process_copy_strings(const char *const *source,
                                        uint32_t count, char ***destination) {
    char **items = NULL;
    uint32_t index;
    int32_t status;
    if (count == 0) {
        *destination = NULL;
        return 0;
    }
    if (source == NULL) return EOS_ERRNO_FAULT;
    if (count > UINT32_MAX / (uint32_t)sizeof(*items)) {
        return EOS_ERRNO_NO_MEMORY;
    }
    status = eos_port_memory_alloc(count * (uint32_t)sizeof(*items),
                                   (void **)&items);
    if (status != EOS_PORT_STATUS_OK) {
        return eos_process_mapped_status(status, "process.vector.allocate");
    }
    (void)memset(items, 0, count * sizeof(*items));
    for (index = 0; index < count; ++index) {
        status = eos_process_copy_string(source[index], &items[index]);
        if (status != 0) {
            eos_process_free_string_array(items, count);
            return status;
        }
    }
    *destination = items;
    return 0;
}

static void eos_process_format_name(char name[32], uint32_t identity) {
    static const char digits[] = "0123456789abcdef";
    uint32_t index;
    (void)memcpy(name, "eos.rust.", 9U);
    for (index = 0; index < UINT32_C(8); ++index) {
        name[9U + index] = digits[(identity >> (28U - index * 4U)) & 0xfU];
    }
    name[17] = '\0';
}

static int32_t eos_process_pin_read(eos_fd_process_pin *pin, void *buffer,
                                    uint32_t byte_count) {
    switch (pin->kind) {
    case EOS_FD_KIND_FILE:
        return eos_fs_file_read((eos_fs_file *)pin->native.pointer, buffer,
                                byte_count, 0, 0);
    case EOS_FD_KIND_PIPE_READER:
        return byte_count == 0 ? 0 : eos_pipe_read(
            (eos_pipe_endpoint *)pin->native.pointer, buffer, byte_count);
    case EOS_FD_KIND_SOCKET:
        return eos_socket_receive_held((eos_socket *)pin->native.pointer,
                                       buffer, byte_count, 0);
    case EOS_FD_KIND_CONSOLE: {
        uint32_t completed = 0;
        int32_t status;
        if (pin->native.word != UINT32_C(0)) {
            return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
        }
        status = eos_port_console_read(pin->native.word, buffer, byte_count,
                                       &completed);
        return eos_fs_io_result(status, completed, "process.console.read", 1);
    }
    case EOS_FD_KIND_NULL:
        return eos_fs_null_read((eos_null_file *)pin->native.pointer);
    default:
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
}

static int32_t eos_process_pin_write(eos_fd_process_pin *pin,
                                     const void *buffer,
                                     uint32_t byte_count) {
    switch (pin->kind) {
    case EOS_FD_KIND_FILE:
        return eos_fs_file_write((eos_fs_file *)pin->native.pointer, buffer,
                                 byte_count, 0, 0);
    case EOS_FD_KIND_PIPE_WRITER:
        return byte_count == 0 ? 0 : eos_pipe_write(
            (eos_pipe_endpoint *)pin->native.pointer, buffer, byte_count);
    case EOS_FD_KIND_SOCKET:
        return eos_socket_send_held((eos_socket *)pin->native.pointer, buffer,
                                    byte_count, 0);
    case EOS_FD_KIND_CONSOLE: {
        uint32_t completed = 0;
        int32_t status;
        if (pin->native.word == UINT32_C(0)) {
            return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
        }
        status = eos_port_console_write(pin->native.word, buffer, byte_count,
                                        &completed);
        return eos_fs_io_result(status, completed, "process.console.write", 0);
    }
    case EOS_FD_KIND_NULL:
        return eos_fs_null_write((eos_null_file *)pin->native.pointer,
                                 byte_count);
    default:
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
}

static int32_t eos_process_read(void *context, void *buffer,
                                uint32_t byte_count) {
    return eos_process_pin_read(&((eos_process_record *)context)->stdio[0],
                                buffer, byte_count);
}

static int32_t eos_process_write_out(void *context, const void *buffer,
                                     uint32_t byte_count) {
    return eos_process_pin_write(&((eos_process_record *)context)->stdio[1],
                                 buffer, byte_count);
}

static int32_t eos_process_write_err(void *context, const void *buffer,
                                     uint32_t byte_count) {
    return eos_process_pin_write(&((eos_process_record *)context)->stdio[2],
                                 buffer, byte_count);
}

static void eos_process_worker(void *opaque) {
    eos_process_record *record = (eos_process_record *)opaque;
    int32_t load_status = eos_port_process_load(&record->request);
    int32_t run_status = load_status;
    int32_t unload_status;
    int32_t exit_code = 0;
    int destroy;

    if (eos_port_sync_lock(record->completion) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    record->load_status = load_status;
    record->loaded = UINT32_C(1);
    if (eos_port_sync_broadcast(record->completion, EOS_PROCESS_EVENT_LOADED) !=
        EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    if (eos_port_sync_unlock(record->completion) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }

    if (load_status == EOS_PORT_STATUS_OK) {
        run_status = eos_port_process_run(&record->request, &exit_code);
        unload_status = eos_port_process_unload(record->name);
        if (run_status == EOS_PORT_STATUS_OK &&
            unload_status != EOS_PORT_STATUS_OK) {
            run_status = unload_status;
        }
    }

    eos_process_release_pins(record);
    if (eos_port_sync_lock(record->completion) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    record->run_status = run_status;
    record->exit_code = exit_code;
    atomic_store_explicit(&record->completed, UINT32_C(1), memory_order_release);
    if (eos_port_sync_broadcast(record->completion,
                                EOS_PROCESS_EVENT_COMPLETED) !=
        EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    if (eos_port_sync_unlock(record->completion) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }

    if (eos_process_registry_lock() != 0) eos_rust_abort();
    destroy = eos_process_unref_locked(record);
    eos_process_registry_unlock();
    if (destroy) eos_process_destroy(record);
}

static int32_t eos_process_acquire(eos_rust_process_t identity,
                                   eos_process_record **output) {
    eos_process_record *record;
    if (eos_process_registry_lock() != 0) return -1;
    record = eos_process_find_locked(identity);
    if (record == NULL) {
        eos_process_registry_unlock();
        return eos_process_fail_errno(EOS_ERRNO_NO_PROCESS);
    }
    if (record->references == UINT32_MAX) {
        eos_process_registry_unlock();
        return eos_process_fail_errno(EOS_ERRNO_WOULD_BLOCK);
    }
    ++record->references;
    eos_process_registry_unlock();
    *output = record;
    return 0;
}

static void eos_process_release(eos_process_record *record) {
    int destroy;
    if (eos_process_registry_lock() != 0) eos_rust_abort();
    destroy = eos_process_unref_locked(record);
    eos_process_registry_unlock();
    if (destroy) eos_process_destroy(record);
}

static int32_t eos_process_status_copy(eos_process_record *record,
                                       eos_rust_process_status *status) {
    if (record->kill_delivered != UINT32_C(0)) {
        (void)memset(status, 0, sizeof(*status));
        status->kind = EOS_RUST_PROCESS_TERMINATED;
        status->code = INT32_C(1);
        return 0;
    }
    if (record->run_status != EOS_PORT_STATUS_OK) {
        return eos_process_fail_status(record->run_status, "process.run");
    }
    (void)memset(status, 0, sizeof(*status));
    status->kind = EOS_RUST_PROCESS_EXITED;
    status->code = record->exit_code;
    return 0;
}

int32_t eos_rust_spawn(const eos_rust_spawn_request *input,
                       eos_rust_process_t *process) {
    eos_process_record *record = NULL;
    int32_t descriptors[3];
    int32_t excluded[3];
    uint32_t capabilities;
    uint32_t index;
    int32_t error = 0;
    int32_t status;
    int destroy = 0;

    if (process == NULL) return eos_process_fail_errno(EOS_ERRNO_FAULT);
    *process = 0;
    if (input == NULL || input->program == NULL) {
        return eos_process_fail_errno(EOS_ERRNO_FAULT);
    }
    if (input->flags != 0) return eos_process_fail_errno(EOS_ERRNO_INVALID);
    for (index = 0; index < UINT32_C(7); ++index) {
        if (input->reserved[index] != 0) {
            return eos_process_fail_errno(EOS_ERRNO_INVALID);
        }
    }
    if ((input->argc != 0 && input->argv == NULL) ||
        (input->envc != 0 && input->envp == NULL) || input->stdin_fd < -1 ||
        input->stdout_fd < -1 || input->stderr_fd < -1) {
        return eos_process_fail_errno(EOS_ERRNO_FAULT);
    }
    capabilities = eos_port_process_capabilities();
    if ((input->envc != 0 &&
         (capabilities & EOS_PORT_PROCESS_CAP_ENVIRONMENT) == 0) ||
        (input->cwd != NULL && (capabilities & EOS_PORT_PROCESS_CAP_CWD) == 0) ||
        (input->stderr_fd != -1 &&
         (capabilities & EOS_PORT_PROCESS_CAP_STDERR) == 0)) {
        return eos_process_fail_errno(EOS_ERRNO_NOT_SUPPORTED);
    }

    status = eos_port_memory_alloc((uint32_t)sizeof(*record), (void **)&record);
    if (status != EOS_PORT_STATUS_OK || record == NULL) {
        return eos_process_fail_status(status, "process.record.allocate");
    }
    (void)memset(record, 0, sizeof(*record));
    status = eos_port_sync_create(&record->completion);
    if (status != EOS_PORT_STATUS_OK) {
        if (eos_port_memory_free(record) != EOS_PORT_STATUS_OK) eos_rust_abort();
        return eos_process_fail_status(status, "process.sync.create");
    }
#ifdef EOS_RUST_HOST_TEST
    (void)atomic_fetch_add_explicit(&eos_process_live_record_count,
                                    UINT32_C(1), memory_order_relaxed);
#endif
    record->request.argc = input->argc;
    record->request.envc = input->envc;
    error = eos_process_copy_string(input->program, &record->program);
    if (error == 0) {
        error = eos_process_copy_strings(input->argv, input->argc, &record->argv);
    }
    if (error == 0) {
        error = eos_process_copy_strings(input->envp, input->envc, &record->envp);
    }
    if (error == 0 && input->cwd != NULL) {
        error = eos_process_copy_string(input->cwd, &record->cwd);
    }
    if (error != 0) {
        eos_process_destroy(record);
        return eos_process_fail_errno(error);
    }

    descriptors[0] = input->stdin_fd == -1 ? 0 : input->stdin_fd;
    descriptors[1] = input->stdout_fd == -1 ? 1 : input->stdout_fd;
    descriptors[2] = input->stderr_fd == -1 ? 2 : input->stderr_fd;
    for (index = 0; index < UINT32_C(3); ++index) {
        if (eos_fd_process_pin_descriptor(descriptors[index],
                                          &record->stdio[index]) != 0) {
            eos_process_destroy(record);
            return -1;
        }
        excluded[index] = input->stdin_fd == -1 && index == 0 ? -1 :
                          input->stdout_fd == -1 && index == 1 ? -1 :
                          input->stderr_fd == -1 && index == 2 ? -1 :
                          descriptors[index];
    }
    if (input->stderr_fd == -1 &&
        (capabilities & EOS_PORT_PROCESS_CAP_STDERR) == 0 &&
        ((capabilities & EOS_PORT_PROCESS_CAP_NATIVE_STDERR) == 0 ||
         !eos_fd_process_pin_is_native_standard(&record->stdio[2],
                                                UINT32_C(2)))) {
        eos_process_destroy(record);
        return eos_process_fail_errno(EOS_ERRNO_NOT_SUPPORTED);
    }
    if (eos_fd_process_snapshot_inheritable(
            record->inherited_pins, EOS_FD_TABLE_CAPACITY,
            &record->inherited_count, excluded, UINT32_C(3)) != 0) {
        eos_process_destroy(record);
        return -1;
    }
    if (record->inherited_count != 0 &&
        (capabilities & EOS_PORT_PROCESS_CAP_DESCRIPTOR_INHERITANCE) == 0) {
        eos_process_destroy(record);
        return eos_process_fail_errno(EOS_ERRNO_NOT_SUPPORTED);
    }
    for (index = 0; index < record->inherited_count; ++index) {
        record->inherited[index].descriptor =
            record->inherited_pins[index].descriptor;
        record->inherited[index].kind =
            (uint32_t)record->inherited_pins[index].kind;
    }
    record->request.program = record->program;
    record->request.argv = record->argv;
    record->request.argc = input->argc;
    record->request.envp = record->envp;
    record->request.envc = input->envc;
    record->request.cwd = record->cwd;
    record->request.io.read = eos_process_read;
    record->request.io.write_out = eos_process_write_out;
    record->request.io.write_err = eos_process_write_err;
    record->request.io.context = record;
    record->request.inherited = record->inherited;
    record->request.inherited_count = record->inherited_count;

    if (eos_process_registry_lock() != 0) {
        eos_process_destroy(record);
        return -1;
    }
    if (eos_process_identity_exhausted != 0) {
        eos_process_registry_unlock();
        eos_process_destroy(record);
        return eos_process_fail_errno(EOS_ERRNO_WOULD_BLOCK);
    }
    record->identity = eos_process_next_identity;
    if (eos_process_next_identity == UINT32_MAX) {
        eos_process_identity_exhausted = UINT32_C(1);
    } else {
        ++eos_process_next_identity;
    }
    eos_process_format_name(record->name, record->identity);
    record->request.name = record->name;
    record->references = UINT32_C(3); /* registry + worker + creator */
    record->in_registry = UINT32_C(1);
    record->next = eos_process_records;
    eos_process_records = record;
    eos_process_registry_unlock();

    status = eos_port_process_validate(&record->request);
    if (status != EOS_PORT_STATUS_OK) {
        if (eos_process_registry_lock() != 0) eos_rust_abort();
        (void)eos_process_drop_registry_locked(record);
        (void)eos_process_unref_locked(record); /* worker never started */
        destroy = eos_process_unref_locked(record); /* creator */
        eos_process_registry_unlock();
        if (destroy) eos_process_destroy(record);
        return eos_process_fail_status(status, "process.validate");
    }

    if (eos_port_sync_lock(record->completion) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    status = eos_port_thread_create(record->name, eos_process_worker, record,
                                    EOS_PROCESS_WORKER_STACK);
    if (status != EOS_PORT_STATUS_OK) {
        if (eos_port_sync_unlock(record->completion) != EOS_PORT_STATUS_OK) {
            eos_rust_abort();
        }
        if (eos_process_registry_lock() != 0) eos_rust_abort();
        (void)eos_process_drop_registry_locked(record);
        (void)eos_process_unref_locked(record); /* worker never started */
        destroy = eos_process_unref_locked(record); /* creator */
        eos_process_registry_unlock();
        if (destroy) eos_process_destroy(record);
        return eos_process_fail_status(status, "process.thread.create");
    }
    while (record->loaded == UINT32_C(0)) {
        status = eos_port_sync_wait(record->completion, EOS_PROCESS_EVENT_LOADED);
        if (status != EOS_PORT_STATUS_OK) break;
    }
    if (eos_port_sync_unlock(record->completion) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    if (status != EOS_PORT_STATUS_OK || record->load_status != EOS_PORT_STATUS_OK) {
        int32_t failure = status != EOS_PORT_STATUS_OK ? status
                                                       : record->load_status;
        if (eos_process_registry_lock() != 0) eos_rust_abort();
        (void)eos_process_drop_registry_locked(record);
        destroy = eos_process_unref_locked(record); /* creator */
        eos_process_registry_unlock();
        if (destroy) eos_process_destroy(record);
        return eos_process_fail_status(failure, "process.load");
    }
    *process = record->identity;
    if (eos_process_registry_lock() != 0) eos_rust_abort();
    destroy = eos_process_unref_locked(record); /* publication complete */
    eos_process_registry_unlock();
    if (destroy) eos_process_destroy(record);
    return 0;
}

int32_t eos_rust_process_wait(eos_rust_process_t identity,
                              eos_rust_process_status *status_output) {
    eos_process_record *record;
    int32_t status;
    int32_t result;
    if (status_output == NULL) return eos_process_fail_errno(EOS_ERRNO_FAULT);
    if (eos_process_acquire(identity, &record) != 0) return -1;
    status = eos_port_sync_lock(record->completion);
    if (status == EOS_PORT_STATUS_OK) {
        while (atomic_load_explicit(&record->completed, memory_order_acquire) == 0) {
            status = eos_port_sync_wait(record->completion,
                                        EOS_PROCESS_EVENT_COMPLETED);
            if (status != EOS_PORT_STATUS_OK) break;
        }
        if (status == EOS_PORT_STATUS_OK) {
            result = eos_process_status_copy(record, status_output);
        } else {
            result = eos_process_fail_status(status, "process.wait");
        }
        if (eos_port_sync_unlock(record->completion) != EOS_PORT_STATUS_OK) {
            eos_rust_abort();
        }
    } else {
        result = eos_process_fail_status(status, "process.wait.lock");
    }
    eos_process_release(record);
    return result;
}

int32_t eos_rust_process_try_wait(eos_rust_process_t identity,
                                  eos_rust_process_status *status_output) {
    eos_process_record *record;
    int32_t status;
    int32_t result;
    if (status_output == NULL) return eos_process_fail_errno(EOS_ERRNO_FAULT);
    if (eos_process_acquire(identity, &record) != 0) return -1;
    status = eos_port_sync_lock(record->completion);
    if (status != EOS_PORT_STATUS_OK) {
        result = eos_process_fail_status(status, "process.try_wait.lock");
    } else if (atomic_load_explicit(&record->completed, memory_order_acquire) == 0) {
        result = 0;
    } else {
        result = eos_process_status_copy(record, status_output) == 0 ? 1 : -1;
    }
    if (status == EOS_PORT_STATUS_OK &&
        eos_port_sync_unlock(record->completion) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
    eos_process_release(record);
    return result;
}

int32_t eos_rust_process_kill(eos_rust_process_t identity) {
    eos_process_record *record;
    int32_t status;
    uint32_t matched;
    if (eos_process_acquire(identity, &record) != 0) return -1;
    for (;;) {
        status = eos_port_sync_lock(record->completion);
        if (status != EOS_PORT_STATUS_OK) {
            status = eos_process_fail_status(status, "process.kill.lock");
            break;
        }
        if (atomic_load_explicit(&record->completed, memory_order_acquire) != 0) {
            if (eos_port_sync_unlock(record->completion) != EOS_PORT_STATUS_OK) {
                eos_rust_abort();
            }
            status = 0;
            break;
        }
        matched = UINT32_C(0);
        status = eos_port_process_kill(record->name, &matched);
        if (matched != UINT32_C(0)) {
            record->kill_delivered = UINT32_C(1);
        }
        if (eos_port_sync_unlock(record->completion) != EOS_PORT_STATUS_OK) {
            eos_rust_abort();
        }
        if (status != EOS_PORT_STATUS_OK) {
            status = eos_process_fail_status(status, "process.kill");
            break;
        }
        if (matched != UINT32_C(0)) {
            status = 0;
            break;
        }
        (void)eos_port_delay_ticks(UINT32_C(1));
    }
    eos_process_release(record);
    return status;
}

int32_t eos_rust_process_close(eos_rust_process_t identity) {
    eos_process_record *record;
    int destroy;
    if (eos_process_registry_lock() != 0) return -1;
    record = eos_process_find_locked(identity);
    if (record == NULL) {
        eos_process_registry_unlock();
        return eos_process_fail_errno(EOS_ERRNO_NO_PROCESS);
    }
    destroy = eos_process_drop_registry_locked(record);
    eos_process_registry_unlock();
    if (destroy) eos_process_destroy(record);
    return 0;
}

#ifdef EOS_RUST_HOST_TEST
uint32_t eos_process_test_live_records(void) {
    return atomic_load_explicit(&eos_process_live_record_count,
                                memory_order_acquire);
}
#endif
