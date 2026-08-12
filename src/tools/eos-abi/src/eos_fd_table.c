#include "eos_fd_table.h"

#include <stddef.h>
#include <stdatomic.h>
#include <string.h>

typedef void (*eos_fd_destructor)(eos_fd_native native);

typedef enum eos_fd_object_state {
    EOS_FD_OBJECT_FREE = 0,
    EOS_FD_OBJECT_LIVE = 1,
    EOS_FD_OBJECT_CLOSING = 2,
} eos_fd_object_state;

typedef struct eos_fd_object {
    eos_fd_object_state state;
    uint32_t generation;
    uint32_t references;
    eos_fd_kind kind;
    eos_fd_native native;
    eos_fd_destructor destructor;
} eos_fd_object;

typedef struct eos_fd_slot {
    uint32_t occupied;
    eos_fd_kind kind;
    uint32_t generation;
    uint32_t reference_count;
    uint32_t flags;
    eos_fd_native native;
    uint32_t object_index;
    uint32_t object_generation;
} eos_fd_slot;

typedef struct eos_fd_pending_destroy {
    uint32_t pending;
    uint32_t object_index;
    uint32_t object_generation;
    eos_fd_native native;
    eos_fd_destructor destructor;
} eos_fd_pending_destroy;

static eos_fd_slot eos_fd_slots[EOS_FD_TABLE_CAPACITY];
static eos_fd_object eos_fd_objects[EOS_FD_TABLE_CAPACITY];
static uint32_t eos_runtime_initialized;

static uint32_t eos_fd_next_generation(uint32_t generation) {
    ++generation;
    return generation == UINT32_C(0) ? UINT32_C(1) : generation;
}

static int32_t eos_fd_fail_errno(int32_t error_number) {
    *eos_port_errno_location() = error_number;
    return -1;
}

static int32_t eos_fd_fail_status(int32_t status, const char *operation) {
    const eos_error_result error =
        eos_error_from_port_status_impl(status, operation);
    return eos_fd_fail_errno(error.kind == EOS_ERROR_ERRNO
                                 ? error.error_number
                                 : EOS_ERRNO_IO);
}

static int32_t eos_fd_lock(void) {
    int32_t status = eos_port_lock_acquire(EOS_PORT_LOCK_FD_TABLE);
    return status == EOS_PORT_STATUS_OK
               ? 0
               : eos_fd_fail_status(status, "descriptor.lock");
}

static void eos_fd_unlock(void) {
    if (eos_port_lock_release(EOS_PORT_LOCK_FD_TABLE) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
}

static void eos_runtime_init_unlock(void) {
    if (eos_port_lock_release(EOS_PORT_LOCK_RUNTIME_INIT) !=
        EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
}

static int eos_fd_kind_valid(eos_fd_kind kind) {
    return kind >= EOS_FD_KIND_CONSOLE && kind <= EOS_FD_KIND_SOCKET;
}

static int32_t eos_fd_find_free_slot_locked(uint32_t first) {
    uint32_t index;
    for (index = first; index < EOS_FD_TABLE_CAPACITY; ++index) {
        if (eos_fd_slots[index].occupied == UINT32_C(0)) {
            return (int32_t)index;
        }
    }
    return -1;
}

static int32_t eos_fd_find_free_object_locked(void) {
    uint32_t index;
    for (index = 0; index < EOS_FD_TABLE_CAPACITY; ++index) {
        if (eos_fd_objects[index].state == EOS_FD_OBJECT_FREE) {
            return (int32_t)index;
        }
    }
    return -1;
}

static void eos_fd_fill_slot_locked(uint32_t slot_index,
                                    uint32_t object_index,
                                    uint32_t flags) {
    eos_fd_slot *slot = &eos_fd_slots[slot_index];
    eos_fd_object *object = &eos_fd_objects[object_index];
    slot->occupied = UINT32_C(1);
    slot->kind = object->kind;
    slot->generation = eos_fd_next_generation(slot->generation);
    slot->reference_count = UINT32_C(1);
    slot->flags = flags & EOS_FD_FLAG_CLOEXEC;
    slot->native = object->native;
    slot->object_index = object_index;
    slot->object_generation = object->generation;
}

static void eos_fd_create_object_locked(uint32_t object_index,
                                        eos_fd_kind kind,
                                        eos_fd_native native,
                                        eos_fd_destructor destructor) {
    eos_fd_object *object = &eos_fd_objects[object_index];
    object->state = EOS_FD_OBJECT_LIVE;
    object->generation = eos_fd_next_generation(object->generation);
    object->references = UINT32_C(1);
    object->kind = kind;
    object->native = native;
    object->destructor = destructor;
}

static eos_fd_pending_destroy eos_fd_drop_object_locked(
    uint32_t object_index,
    uint32_t object_generation) {
    eos_fd_pending_destroy pending = {0, 0, 0, {0}, NULL};
    eos_fd_object *object;
    if (object_index >= EOS_FD_TABLE_CAPACITY) return pending;
    object = &eos_fd_objects[object_index];
    if (object->state != EOS_FD_OBJECT_LIVE ||
        object->generation != object_generation ||
        object->references == UINT32_C(0)) {
        return pending;
    }
    --object->references;
    if (object->references == UINT32_C(0)) {
        object->state = EOS_FD_OBJECT_CLOSING;
        pending.pending = UINT32_C(1);
        pending.object_index = object_index;
        pending.object_generation = object_generation;
        pending.native = object->native;
        pending.destructor = object->destructor;
    }
    return pending;
}

static int32_t eos_fd_finish_destroy(eos_fd_pending_destroy pending) {
    eos_fd_object *object;
    if (pending.pending == UINT32_C(0)) return 0;
    if (pending.destructor != NULL) pending.destructor(pending.native);
    if (eos_fd_lock() != 0) return -1;
    object = &eos_fd_objects[pending.object_index];
    if (object->state != EOS_FD_OBJECT_CLOSING ||
        object->generation != pending.object_generation ||
        object->references != UINT32_C(0)) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_IO);
    }
    object->state = EOS_FD_OBJECT_FREE;
    object->kind = EOS_FD_KIND_NONE;
    object->native.word = (uintptr_t)0;
    object->destructor = NULL;
    eos_fd_unlock();
    return 0;
}

static eos_fd_pending_destroy eos_fd_remove_slot_locked(uint32_t slot_index) {
    eos_fd_slot *slot = &eos_fd_slots[slot_index];
    eos_fd_pending_destroy pending = eos_fd_drop_object_locked(
        slot->object_index, slot->object_generation);
    slot->occupied = UINT32_C(0);
    slot->kind = EOS_FD_KIND_NONE;
    slot->reference_count = UINT32_C(0);
    slot->flags = UINT32_C(0);
    slot->native.word = (uintptr_t)0;
    slot->object_index = UINT32_MAX;
    slot->object_generation = UINT32_C(0);
    return pending;
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_allocate(
    eos_fd_kind kind,
    eos_fd_native native,
    eos_fd_destructor destructor,
    uint32_t flags) {
    int32_t slot_index;
    int32_t object_index;
    if (!eos_fd_kind_valid(kind)) {
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    if (eos_fd_lock() != 0) return -1;
    slot_index = eos_fd_find_free_slot_locked(UINT32_C(3));
    object_index = eos_fd_find_free_object_locked();
    if (slot_index < 0 || object_index < 0) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_TOO_MANY_OPEN_FILES);
    }
    eos_fd_create_object_locked((uint32_t)object_index,
                                kind,
                                native,
                                destructor);
    eos_fd_fill_slot_locked((uint32_t)slot_index,
                            (uint32_t)object_index,
                            flags);
    eos_fd_unlock();
    return slot_index;
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_close(int32_t descriptor) {
    eos_fd_pending_destroy pending;
    if (descriptor < 0 || (uint32_t)descriptor >= EOS_FD_TABLE_CAPACITY) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    if (eos_fd_slots[(uint32_t)descriptor].occupied == UINT32_C(0)) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    pending = eos_fd_remove_slot_locked((uint32_t)descriptor);
    eos_fd_unlock();
    return eos_fd_finish_destroy(pending);
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_dup(int32_t descriptor) {
    int32_t new_descriptor;
    eos_fd_slot *source;
    eos_fd_object *object;
    if (descriptor < 0 || (uint32_t)descriptor >= EOS_FD_TABLE_CAPACITY) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    source = &eos_fd_slots[(uint32_t)descriptor];
    if (source->occupied == UINT32_C(0)) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    new_descriptor = eos_fd_find_free_slot_locked(UINT32_C(3));
    if (new_descriptor < 0) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_TOO_MANY_OPEN_FILES);
    }
    object = &eos_fd_objects[source->object_index];
    if (object->state != EOS_FD_OBJECT_LIVE ||
        object->generation != source->object_generation) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    ++object->references;
    eos_fd_fill_slot_locked((uint32_t)new_descriptor,
                            source->object_index,
                            UINT32_C(0));
    eos_fd_unlock();
    return new_descriptor;
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_dup2(
    int32_t old_descriptor,
    int32_t new_descriptor) {
    eos_fd_pending_destroy pending = {0, 0, 0, {0}, NULL};
    eos_fd_slot *source;
    eos_fd_object *object;
    if (old_descriptor < 0 || new_descriptor < 0 ||
        (uint32_t)old_descriptor >= EOS_FD_TABLE_CAPACITY ||
        (uint32_t)new_descriptor >= EOS_FD_TABLE_CAPACITY) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    source = &eos_fd_slots[(uint32_t)old_descriptor];
    if (source->occupied == UINT32_C(0) ||
        ((uint32_t)new_descriptor < UINT32_C(3) &&
         eos_runtime_initialized == UINT32_C(0))) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (old_descriptor == new_descriptor) {
        eos_fd_unlock();
        return new_descriptor;
    }
    object = &eos_fd_objects[source->object_index];
    if (object->state != EOS_FD_OBJECT_LIVE ||
        object->generation != source->object_generation) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    ++object->references;
    if (eos_fd_slots[(uint32_t)new_descriptor].occupied != UINT32_C(0)) {
        pending = eos_fd_remove_slot_locked((uint32_t)new_descriptor);
    }
    eos_fd_fill_slot_locked((uint32_t)new_descriptor,
                            source->object_index,
                            UINT32_C(0));
    eos_fd_unlock();
    if (eos_fd_finish_destroy(pending) != 0) return -1;
    return new_descriptor;
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_get_flags(int32_t descriptor,
                                                      uint32_t *flags) {
    if (flags == NULL) return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    if (descriptor < 0 || (uint32_t)descriptor >= EOS_FD_TABLE_CAPACITY) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    if (eos_fd_slots[(uint32_t)descriptor].occupied == UINT32_C(0)) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    *flags = eos_fd_slots[(uint32_t)descriptor].flags;
    eos_fd_unlock();
    return 0;
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_set_cloexec(int32_t descriptor,
                                                        int32_t enabled) {
    if (descriptor < 0 || (uint32_t)descriptor >= EOS_FD_TABLE_CAPACITY) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    if (eos_fd_slots[(uint32_t)descriptor].occupied == UINT32_C(0)) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    eos_fd_slots[(uint32_t)descriptor].flags =
        enabled == 0 ? UINT32_C(0) : EOS_FD_FLAG_CLOEXEC;
    eos_fd_unlock();
    return 0;
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_get_token(int32_t descriptor,
                                                      eos_fd_token *token) {
    if (token == NULL) return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    if (descriptor < 0 || (uint32_t)descriptor >= EOS_FD_TABLE_CAPACITY) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    if (eos_fd_slots[(uint32_t)descriptor].occupied == UINT32_C(0)) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    token->descriptor = descriptor;
    token->generation = eos_fd_slots[(uint32_t)descriptor].generation;
    eos_fd_unlock();
    return 0;
}

static int32_t eos_fd_acquire_impl(int32_t descriptor,
                                   uint32_t generation,
                                   int generation_required,
                                   eos_fd_kind required_kind,
                                   eos_fd_reference *reference) {
    eos_fd_slot *slot;
    eos_fd_object *object;
    if (reference == NULL) return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    (void)memset(reference, 0, sizeof(*reference));
    if (descriptor < 0 || (uint32_t)descriptor >= EOS_FD_TABLE_CAPACITY) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    slot = &eos_fd_slots[(uint32_t)descriptor];
    if (slot->occupied == UINT32_C(0) ||
        (generation_required && slot->generation != generation) ||
        (required_kind != EOS_FD_KIND_NONE && slot->kind != required_kind)) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    object = &eos_fd_objects[slot->object_index];
    if (object->state != EOS_FD_OBJECT_LIVE ||
        object->generation != slot->object_generation ||
        object->kind != slot->kind) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    ++object->references;
    ++slot->reference_count;
    reference->slot_index = (uint32_t)descriptor;
    reference->slot_generation = slot->generation;
    reference->object_index = slot->object_index;
    reference->object_generation = slot->object_generation;
    reference->active = UINT32_C(1);
    reference->kind = slot->kind;
    reference->native = object->native;
    eos_fd_unlock();
    return 0;
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_acquire(
    int32_t descriptor,
    eos_fd_kind required_kind,
    eos_fd_reference *reference) {
    return eos_fd_acquire_impl(descriptor,
                               UINT32_C(0),
                               0,
                               required_kind,
                               reference);
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_acquire_token(
    eos_fd_token token,
    eos_fd_kind required_kind,
    eos_fd_reference *reference) {
    return eos_fd_acquire_impl(token.descriptor,
                               token.generation,
                               1,
                               required_kind,
                               reference);
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_release(
    eos_fd_reference *reference) {
    eos_fd_object *object;
    eos_fd_slot *slot;
    eos_fd_pending_destroy pending;
    if (reference == NULL || reference->active == UINT32_C(0) ||
        reference->object_index >= EOS_FD_TABLE_CAPACITY) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    object = &eos_fd_objects[reference->object_index];
    if ((object->state != EOS_FD_OBJECT_LIVE &&
         object->state != EOS_FD_OBJECT_CLOSING) ||
        object->generation != reference->object_generation ||
        object->references == UINT32_C(0)) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (reference->slot_index < EOS_FD_TABLE_CAPACITY) {
        slot = &eos_fd_slots[reference->slot_index];
        if (slot->occupied != UINT32_C(0) &&
            slot->generation == reference->slot_generation &&
            slot->reference_count > UINT32_C(1)) {
            --slot->reference_count;
        }
    }
    reference->active = UINT32_C(0);
    pending = eos_fd_drop_object_locked(reference->object_index,
                                        reference->object_generation);
    eos_fd_unlock();
    return eos_fd_finish_destroy(pending);
}

static void eos_fd_console_destroy(eos_fd_native native) {
    eos_port_console_release(native.word);
}

static int32_t eos_fd_install_standard_locked(const uintptr_t consoles[3]) {
    uint32_t object_indices[3];
    uint32_t found = 0;
    uint32_t index;
    for (index = 0; index < UINT32_C(3); ++index) {
        if (eos_fd_slots[index].occupied != UINT32_C(0)) return -1;
    }
    for (index = 0; index < EOS_FD_TABLE_CAPACITY && found < UINT32_C(3);
         ++index) {
        if (eos_fd_objects[index].state == EOS_FD_OBJECT_FREE) {
            object_indices[found++] = index;
        }
    }
    if (found != UINT32_C(3)) return -1;
    for (index = 0; index < UINT32_C(3); ++index) {
        eos_fd_native native;
        native.word = consoles[index];
        eos_fd_create_object_locked(object_indices[index],
                                    EOS_FD_KIND_CONSOLE,
                                    native,
                                    eos_fd_console_destroy);
        eos_fd_fill_slot_locked(index, object_indices[index], UINT32_C(0));
    }
    return 0;
}

static EOS_RUST_NORETURN void eos_runtime_init_abort(uint32_t stream) {
    static const char *const messages[4] = {
        "EOS Rust runtime initialization failed: standard descriptor 0\n",
        "EOS Rust runtime initialization failed: standard descriptor 1\n",
        "EOS Rust runtime initialization failed: standard descriptor 2\n",
        "EOS Rust runtime initialization failed: descriptor table\n",
    };
    eos_port_direct_diagnostic(messages[stream < UINT32_C(3) ? stream
                                                              : UINT32_C(3)]);
    eos_rust_abort();
}

void eos_rust_runtime_init(int32_t argc, const char *const *argv) {
    uintptr_t consoles[3] = {0, 0, 0};
    uint32_t stream;
    int32_t status;
    (void)argc;
    (void)argv;

    status = eos_port_lock_acquire(EOS_PORT_LOCK_RUNTIME_INIT);
    if (status != EOS_PORT_STATUS_OK) eos_runtime_init_abort(UINT32_C(3));
    if (eos_runtime_initialized != UINT32_C(0)) {
        eos_runtime_init_unlock();
        return;
    }
    for (stream = 0; stream < UINT32_C(3); ++stream) {
        status = eos_port_console_establish(stream, &consoles[stream]);
        if (status != EOS_PORT_STATUS_OK) {
            uint32_t opened;
            for (opened = 0; opened < stream; ++opened) {
                eos_port_console_release(consoles[opened]);
            }
            eos_runtime_init_abort(stream);
        }
    }
    if (eos_fd_lock() != 0) {
        for (stream = 0; stream < UINT32_C(3); ++stream) {
            eos_port_console_release(consoles[stream]);
        }
        eos_runtime_init_abort(UINT32_C(3));
    }
    if (eos_fd_install_standard_locked(consoles) != 0) {
        eos_fd_unlock();
        for (stream = 0; stream < UINT32_C(3); ++stream) {
            eos_port_console_release(consoles[stream]);
        }
        eos_runtime_init_abort(UINT32_C(3));
    }
    eos_runtime_initialized = UINT32_C(1);
    eos_fd_unlock();
    eos_runtime_init_unlock();
}

#ifdef EOS_RUST_HOST_TEST
#define EOS_FD_TEST_IDENTITY_CAPACITY UINT32_C(16384)
static _Atomic uint32_t
    eos_fd_test_destruction_counts[EOS_FD_TEST_IDENTITY_CAPACITY];

static void eos_fd_test_destroy(eos_fd_native native) {
    uint64_t identity = (uint64_t)native.word;
    if (identity < EOS_FD_TEST_IDENTITY_CAPACITY) {
        (void)atomic_fetch_add_explicit(
            &eos_fd_test_destruction_counts[(uint32_t)identity],
            UINT32_C(1), memory_order_relaxed);
    }
}

void eos_fd_test_reset(void) {
    eos_fd_pending_destroy pending[EOS_FD_TABLE_CAPACITY];
    uint32_t pending_count = 0;
    uint32_t index;
    int32_t status = eos_port_lock_acquire(EOS_PORT_LOCK_RUNTIME_INIT);
    if (status != EOS_PORT_STATUS_OK) eos_rust_abort();
    if (eos_fd_lock() != 0) eos_rust_abort();
    for (index = 0; index < EOS_FD_TABLE_CAPACITY; ++index) {
        if (eos_fd_objects[index].state != EOS_FD_OBJECT_FREE) {
            pending[pending_count].pending = UINT32_C(1);
            pending[pending_count].object_index = index;
            pending[pending_count].object_generation =
                eos_fd_objects[index].generation;
            pending[pending_count].native = eos_fd_objects[index].native;
            pending[pending_count].destructor =
                eos_fd_objects[index].destructor;
            ++pending_count;
        }
    }
    (void)memset(eos_fd_slots, 0, sizeof(eos_fd_slots));
    (void)memset(eos_fd_objects, 0, sizeof(eos_fd_objects));
    eos_runtime_initialized = UINT32_C(0);
    eos_fd_unlock();
    eos_runtime_init_unlock();
    for (index = 0; index < pending_count; ++index) {
        if (pending[index].destructor != NULL) {
            pending[index].destructor(pending[index].native);
        }
    }
    for (index = 0; index < EOS_FD_TEST_IDENTITY_CAPACITY; ++index) {
        atomic_store_explicit(&eos_fd_test_destruction_counts[index],
                              UINT32_C(0), memory_order_relaxed);
    }
}

uint32_t eos_fd_test_capacity(void) { return EOS_FD_TABLE_CAPACITY; }

int32_t eos_fd_test_allocate(eos_fd_kind kind,
                             uint64_t native_identity,
                             uint32_t descriptor_flags) {
    eos_fd_native native;
    native.word = (uintptr_t)native_identity;
    return eos_fd_allocate(kind, native, eos_fd_test_destroy, descriptor_flags);
}

int32_t eos_fd_test_close(int32_t descriptor) {
    return eos_fd_close(descriptor);
}

int32_t eos_fd_test_dup(int32_t descriptor) { return eos_fd_dup(descriptor); }

int32_t eos_fd_test_dup2(int32_t old_descriptor, int32_t new_descriptor) {
    return eos_fd_dup2(old_descriptor, new_descriptor);
}

int32_t eos_fd_test_get_flags(int32_t descriptor, uint32_t *flags) {
    return eos_fd_get_flags(descriptor, flags);
}

int32_t eos_fd_test_set_cloexec(int32_t descriptor, int32_t enabled) {
    return eos_fd_set_cloexec(descriptor, enabled);
}

int32_t eos_fd_test_get_token(int32_t descriptor, eos_fd_token *token) {
    return eos_fd_get_token(descriptor, token);
}

int32_t eos_fd_test_acquire(int32_t descriptor,
                            eos_fd_kind required_kind,
                            eos_fd_reference *reference) {
    return eos_fd_acquire(descriptor, required_kind, reference);
}

int32_t eos_fd_test_acquire_token(eos_fd_token token,
                                  eos_fd_kind required_kind,
                                  eos_fd_reference *reference) {
    return eos_fd_acquire_token(token, required_kind, reference);
}

int32_t eos_fd_test_reference_identity(const eos_fd_reference *reference,
                                       uint64_t *native_identity) {
    if (reference == NULL || reference->active == UINT32_C(0) ||
        native_identity == NULL) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    *native_identity = (uint64_t)reference->native.word;
    return 0;
}

int32_t eos_fd_test_release(eos_fd_reference *reference) {
    return eos_fd_release(reference);
}

uint32_t eos_fd_test_destructor_count(uint64_t native_identity) {
    if (native_identity >= EOS_FD_TEST_IDENTITY_CAPACITY) return UINT32_C(0);
    return atomic_load_explicit(
        &eos_fd_test_destruction_counts[(uint32_t)native_identity],
        memory_order_relaxed);
}

int32_t eos_fd_test_is_kind(int32_t descriptor, eos_fd_kind kind) {
    eos_fd_reference reference;
    if (eos_fd_acquire(descriptor, kind, &reference) != 0) return 0;
    if (eos_fd_release(&reference) != 0) return 0;
    return 1;
}
#endif
