#include "eos_fd_table.h"

#include <stddef.h>
#include <stdatomic.h>
#include <string.h>

typedef void (*eos_fd_destructor)(eos_fd_native native);

typedef struct eos_fd_object {
    uint64_t references;
    eos_fd_kind kind;
    eos_fd_native native;
    eos_fd_destructor destructor;
    struct eos_fd_object *next;
} eos_fd_object;

typedef struct eos_fd_lease {
    struct eos_fd_lease *next;
    eos_fd_object *object;
    uint64_t id;
} eos_fd_lease;

typedef struct eos_fd_slot {
    uint32_t occupied;
    eos_fd_kind kind;
    uint32_t generation;
    uint64_t reference_count;
    uint32_t flags;
    eos_fd_native native;
    eos_fd_object *object;
} eos_fd_slot;

typedef struct eos_fd_pending_destroy {
    uint32_t pending;
    eos_fd_native native;
    eos_fd_destructor destructor;
    eos_fd_object *object;
} eos_fd_pending_destroy;

static eos_fd_slot eos_fd_slots[EOS_FD_TABLE_CAPACITY];
static eos_fd_object *eos_fd_all_objects;
static eos_fd_lease *eos_fd_active_leases;
static uint64_t eos_fd_next_lease_id = UINT64_C(1);
static uint32_t eos_fd_lease_ids_exhausted;
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
    return kind >= EOS_FD_KIND_CONSOLE && kind <= EOS_FD_KIND_NULL;
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

static void eos_fd_fill_slot_locked(uint32_t slot_index,
                                    eos_fd_object *object,
                                    uint32_t flags) {
    eos_fd_slot *slot = &eos_fd_slots[slot_index];
    slot->occupied = UINT32_C(1);
    slot->kind = object->kind;
    slot->generation = eos_fd_next_generation(slot->generation);
    slot->reference_count = UINT32_C(1);
    slot->flags = flags & EOS_FD_FLAG_CLOEXEC;
    slot->native = object->native;
    slot->object = object;
}

static void eos_fd_create_object(eos_fd_object *object,
                                 eos_fd_kind kind,
                                 eos_fd_native native,
                                 eos_fd_destructor destructor) {
    object->references = UINT64_C(1);
    object->kind = kind;
    object->native = native;
    object->destructor = destructor;
    object->next = NULL;
}

static void eos_fd_publish_object_locked(eos_fd_object *object) {
    object->next = eos_fd_all_objects;
    eos_fd_all_objects = object;
}

static void eos_fd_unlink_object_locked(eos_fd_object *object) {
    eos_fd_object **link = &eos_fd_all_objects;
    while (*link != NULL && *link != object) {
        link = &(*link)->next;
    }
    if (*link != object) eos_rust_abort();
    *link = object->next;
    object->next = NULL;
}

static eos_fd_pending_destroy eos_fd_drop_object_locked(eos_fd_object *object) {
    eos_fd_pending_destroy pending = {0, {0}, NULL, NULL};
    if (object == NULL || object->references == UINT64_C(0)) return pending;
    --object->references;
    if (object->references == UINT64_C(0)) {
        eos_fd_unlink_object_locked(object);
        pending.pending = UINT32_C(1);
        pending.native = object->native;
        pending.destructor = object->destructor;
        pending.object = object;
    }
    return pending;
}

static void eos_fd_finish_destroy(eos_fd_pending_destroy pending) {
    if (pending.pending == UINT32_C(0)) return;
    if (pending.destructor != NULL) pending.destructor(pending.native);
    if (eos_port_memory_free(pending.object) != EOS_PORT_STATUS_OK) {
        eos_rust_abort();
    }
}

static eos_fd_pending_destroy eos_fd_remove_slot_locked(uint32_t slot_index) {
    eos_fd_slot *slot = &eos_fd_slots[slot_index];
    eos_fd_pending_destroy pending = eos_fd_drop_object_locked(slot->object);
    slot->occupied = UINT32_C(0);
    slot->kind = EOS_FD_KIND_NONE;
    slot->reference_count = UINT32_C(0);
    slot->flags = UINT32_C(0);
    slot->native.word = (uintptr_t)0;
    slot->object = NULL;
    return pending;
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_allocate(
    eos_fd_kind kind,
    eos_fd_native native,
    eos_fd_destructor destructor,
    uint32_t flags) {
    int32_t slot_index;
    int32_t status;
    eos_fd_object *object = NULL;
    if (!eos_fd_kind_valid(kind)) {
        return eos_fd_fail_errno(EOS_ERRNO_INVALID);
    }
    if (eos_fd_lock() != 0) return -1;
    slot_index = eos_fd_find_free_slot_locked(UINT32_C(3));
    eos_fd_unlock();
    if (slot_index < 0) {
        return eos_fd_fail_errno(EOS_ERRNO_TOO_MANY_OPEN_FILES);
    }
    status = eos_port_memory_alloc((uint32_t)sizeof(*object),
                                   (void **)&object);
    if (status != EOS_PORT_STATUS_OK) {
        return eos_fd_fail_status(status, "descriptor.object.allocate");
    }
    eos_fd_create_object(object, kind, native, destructor);
    if (eos_fd_lock() != 0) {
        if (eos_port_memory_free(object) != EOS_PORT_STATUS_OK) {
            eos_rust_abort();
        }
        return -1;
    }
    slot_index = eos_fd_find_free_slot_locked(UINT32_C(3));
    if (slot_index < 0) {
        eos_fd_unlock();
        if (eos_port_memory_free(object) != EOS_PORT_STATUS_OK) {
            eos_rust_abort();
        }
        return eos_fd_fail_errno(EOS_ERRNO_TOO_MANY_OPEN_FILES);
    }
    eos_fd_fill_slot_locked((uint32_t)slot_index,
                            object,
                            flags);
    eos_fd_publish_object_locked(object);
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
    eos_fd_finish_destroy(pending);
    return 0;
}

/*
 * Closes only the slot validated by this exact lease. The lease and slot are
 * consumed under one table lock so a concurrent close/reuse cannot redirect
 * a typed close onto the replacement descriptor.
 */
static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_close_reference(
    eos_fd_reference *reference,
    eos_fd_kind required_kind) {
    eos_fd_lease *lease;
    eos_fd_lease **lease_link;
    eos_fd_object *object;
    eos_fd_slot *slot;
    eos_fd_pending_destroy slot_pending;
    eos_fd_pending_destroy lease_pending;
    int matches;
    if (reference == NULL || reference->active == UINT32_C(0) ||
        reference->lease_id == UINT64_C(0) ||
        reference->slot_index >= EOS_FD_TABLE_CAPACITY ||
        reference->kind != required_kind) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    lease_link = &eos_fd_active_leases;
    while (*lease_link != NULL &&
           (*lease_link)->id != reference->lease_id) {
        lease_link = &(*lease_link)->next;
    }
    if (*lease_link == NULL) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    lease = *lease_link;
    object = lease->object;
    slot = &eos_fd_slots[reference->slot_index];
    matches = slot->occupied != UINT32_C(0) &&
              slot->generation == reference->slot_generation &&
              slot->kind == required_kind &&
              slot->object == object;
    slot_pending.pending = UINT32_C(0);
    slot_pending.native.word = (uintptr_t)0;
    slot_pending.destructor = NULL;
    slot_pending.object = NULL;
    if (matches) {
        slot_pending = eos_fd_remove_slot_locked(reference->slot_index);
        if (slot_pending.pending != UINT32_C(0)) eos_rust_abort();
    }
    *lease_link = lease->next;
    reference->active = UINT32_C(0);
    reference->lease_id = UINT64_C(0);
    lease_pending = eos_fd_drop_object_locked(object);
    eos_fd_unlock();
    eos_fd_finish_destroy(lease_pending);
    if (eos_port_memory_free(lease) != EOS_PORT_STATUS_OK) eos_rust_abort();
    return matches ? 0 : eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
}

static int32_t eos_fd_dup_min(int32_t descriptor, int32_t minimum,
                              uint32_t flags) {
    int32_t new_descriptor;
    eos_fd_slot *source;
    eos_fd_object *object;
    if (descriptor < 0 || (uint32_t)descriptor >= EOS_FD_TABLE_CAPACITY ||
        minimum < 0 || (uint32_t)minimum >= EOS_FD_TABLE_CAPACITY) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    source = &eos_fd_slots[(uint32_t)descriptor];
    if (source->occupied == UINT32_C(0)) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (minimum < 3) minimum = 3;
    new_descriptor = eos_fd_find_free_slot_locked((uint32_t)minimum);
    if (new_descriptor < 0) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_TOO_MANY_OPEN_FILES);
    }
    object = source->object;
    if (object->references == UINT64_MAX) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_WOULD_BLOCK);
    }
    ++object->references;
    eos_fd_fill_slot_locked((uint32_t)new_descriptor,
                            object,
                            flags & EOS_FD_FLAG_CLOEXEC);
    eos_fd_unlock();
    return new_descriptor;
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_dup(int32_t descriptor) {
    return eos_fd_dup_min(descriptor, 3, UINT32_C(0));
}

static EOS_RUST_MAYBE_UNUSED int32_t eos_fd_dup2(
    int32_t old_descriptor,
    int32_t new_descriptor) {
    eos_fd_pending_destroy pending = {0, {0}, NULL, NULL};
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
    object = source->object;
    if (object->references == UINT64_MAX) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_WOULD_BLOCK);
    }
    ++object->references;
    if (eos_fd_slots[(uint32_t)new_descriptor].occupied != UINT32_C(0)) {
        pending = eos_fd_remove_slot_locked((uint32_t)new_descriptor);
    }
    eos_fd_fill_slot_locked((uint32_t)new_descriptor,
                            object,
                            UINT32_C(0));
    eos_fd_unlock();
    eos_fd_finish_destroy(pending);
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
    eos_fd_lease *lease = NULL;
    uint32_t observed_generation;
    int32_t status;
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
    if (slot->object == NULL || slot->object->kind != slot->kind) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lease_ids_exhausted != UINT32_C(0) ||
        slot->object->references == UINT64_MAX ||
        slot->reference_count == UINT64_MAX) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_WOULD_BLOCK);
    }
    observed_generation = slot->generation;
    eos_fd_unlock();
    status = eos_port_memory_alloc((uint32_t)sizeof(*lease), (void **)&lease);
    if (status != EOS_PORT_STATUS_OK) {
        return eos_fd_fail_status(status, "descriptor.lease.allocate");
    }
    if (eos_fd_lock() != 0) {
        if (eos_port_memory_free(lease) != EOS_PORT_STATUS_OK) eos_rust_abort();
        return -1;
    }
    slot = &eos_fd_slots[(uint32_t)descriptor];
    if (slot->occupied == UINT32_C(0) ||
        slot->generation != observed_generation ||
        (required_kind != EOS_FD_KIND_NONE && slot->kind != required_kind)) {
        eos_fd_unlock();
        if (eos_port_memory_free(lease) != EOS_PORT_STATUS_OK) eos_rust_abort();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    object = slot->object;
    if (object == NULL || object->kind != slot->kind) {
        eos_fd_unlock();
        if (eos_port_memory_free(lease) != EOS_PORT_STATUS_OK) eos_rust_abort();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lease_ids_exhausted != UINT32_C(0) ||
        object->references == UINT64_MAX ||
        slot->reference_count == UINT64_MAX) {
        eos_fd_unlock();
        if (eos_port_memory_free(lease) != EOS_PORT_STATUS_OK) eos_rust_abort();
        return eos_fd_fail_errno(EOS_ERRNO_WOULD_BLOCK);
    }
    ++object->references;
    ++slot->reference_count;
    lease->object = object;
    lease->id = eos_fd_next_lease_id;
    lease->next = eos_fd_active_leases;
    eos_fd_active_leases = lease;
    if (eos_fd_next_lease_id == UINT64_MAX) {
        eos_fd_lease_ids_exhausted = UINT32_C(1);
    } else {
        ++eos_fd_next_lease_id;
    }
    reference->slot_index = (uint32_t)descriptor;
    reference->slot_generation = slot->generation;
    reference->lease_id = lease->id;
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
    eos_fd_lease *lease;
    eos_fd_lease **lease_link;
    eos_fd_slot *slot;
    eos_fd_pending_destroy pending;
    if (reference == NULL || reference->active == UINT32_C(0) ||
        reference->lease_id == UINT64_C(0)) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    lease_link = &eos_fd_active_leases;
    while (*lease_link != NULL &&
           (*lease_link)->id != reference->lease_id) {
        lease_link = &(*lease_link)->next;
    }
    if (*lease_link == NULL) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    lease = *lease_link;
    *lease_link = lease->next;
    object = lease->object;
    if (reference->slot_index < EOS_FD_TABLE_CAPACITY) {
        slot = &eos_fd_slots[reference->slot_index];
        if (slot->occupied != UINT32_C(0) &&
            slot->generation == reference->slot_generation &&
            slot->reference_count > UINT64_C(1)) {
            --slot->reference_count;
        }
    }
    reference->active = UINT32_C(0);
    reference->lease_id = UINT64_C(0);
    pending = eos_fd_drop_object_locked(object);
    eos_fd_unlock();
    eos_fd_finish_destroy(pending);
    if (eos_port_memory_free(lease) != EOS_PORT_STATUS_OK) eos_rust_abort();
    return 0;
}

static EOS_RUST_MAYBE_UNUSED void eos_fd_release_or_abort(
    eos_fd_reference *reference) {
    if (eos_fd_release(reference) != 0) eos_rust_abort();
}

static void eos_fd_console_destroy(eos_fd_native native) {
    eos_port_console_release(native.word);
}

static int32_t eos_fd_install_standard_locked(eos_fd_object *const objects[3]) {
    uint32_t index;
    for (index = 0; index < UINT32_C(3); ++index) {
        if (eos_fd_slots[index].occupied != UINT32_C(0)) return -1;
    }
    for (index = 0; index < UINT32_C(3); ++index) {
        eos_fd_fill_slot_locked(index, objects[index], UINT32_C(0));
        eos_fd_publish_object_locked(objects[index]);
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
    eos_fd_object *objects[3] = {NULL, NULL, NULL};
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
    for (stream = 0; stream < UINT32_C(3); ++stream) {
        eos_fd_native native;
        status = eos_port_memory_alloc((uint32_t)sizeof(*objects[stream]),
                                       (void **)&objects[stream]);
        if (status != EOS_PORT_STATUS_OK) {
            uint32_t allocated;
            for (allocated = 0; allocated < stream; ++allocated) {
                if (eos_port_memory_free(objects[allocated]) !=
                    EOS_PORT_STATUS_OK) {
                    eos_rust_abort();
                }
            }
            for (allocated = 0; allocated < UINT32_C(3); ++allocated) {
                eos_port_console_release(consoles[allocated]);
            }
            eos_runtime_init_abort(UINT32_C(3));
        }
        native.word = consoles[stream];
        eos_fd_create_object(objects[stream],
                             EOS_FD_KIND_CONSOLE,
                             native,
                             eos_fd_console_destroy);
    }
    if (eos_fd_lock() != 0) {
        for (stream = 0; stream < UINT32_C(3); ++stream) {
            if (eos_port_memory_free(objects[stream]) != EOS_PORT_STATUS_OK) {
                eos_rust_abort();
            }
            eos_port_console_release(consoles[stream]);
        }
        eos_runtime_init_abort(UINT32_C(3));
    }
    if (eos_fd_install_standard_locked(objects) != 0) {
        eos_fd_unlock();
        for (stream = 0; stream < UINT32_C(3); ++stream) {
            if (eos_port_memory_free(objects[stream]) != EOS_PORT_STATUS_OK) {
                eos_rust_abort();
            }
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
    eos_fd_object *objects;
    eos_fd_lease *leases;
    uint32_t index;
    int32_t status = eos_port_lock_acquire(EOS_PORT_LOCK_RUNTIME_INIT);
    if (status != EOS_PORT_STATUS_OK) eos_rust_abort();
    if (eos_fd_lock() != 0) eos_rust_abort();
    objects = eos_fd_all_objects;
    leases = eos_fd_active_leases;
    eos_fd_all_objects = NULL;
    eos_fd_active_leases = NULL;
    (void)memset(eos_fd_slots, 0, sizeof(eos_fd_slots));
    eos_fd_next_lease_id = UINT64_C(1);
    eos_fd_lease_ids_exhausted = UINT32_C(0);
    eos_runtime_initialized = UINT32_C(0);
    eos_fd_unlock();
    eos_runtime_init_unlock();
    while (leases != NULL) {
        eos_fd_lease *next = leases->next;
        if (eos_port_memory_free(leases) != EOS_PORT_STATUS_OK) eos_rust_abort();
        leases = next;
    }
    while (objects != NULL) {
        eos_fd_object *next = objects->next;
        if (objects->destructor != NULL) {
            objects->destructor(objects->native);
        }
        if (eos_port_memory_free(objects) != EOS_PORT_STATUS_OK) eos_rust_abort();
        objects = next;
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
    eos_fd_lease *lease;
    if (reference == NULL || reference->active == UINT32_C(0) ||
        reference->lease_id == UINT64_C(0) || native_identity == NULL) {
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    if (eos_fd_lock() != 0) return -1;
    lease = eos_fd_active_leases;
    while (lease != NULL && lease->id != reference->lease_id) {
        lease = lease->next;
    }
    if (lease == NULL) {
        eos_fd_unlock();
        return eos_fd_fail_errno(EOS_ERRNO_BAD_DESCRIPTOR);
    }
    *native_identity = (uint64_t)lease->object->native.word;
    eos_fd_unlock();
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

void eos_fd_test_exhaust_lease_ids_after_next_acquire(void) {
    if (eos_fd_lock() != 0) eos_rust_abort();
    eos_fd_next_lease_id = UINT64_MAX;
    eos_fd_lease_ids_exhausted = UINT32_C(0);
    eos_fd_unlock();
}
#endif
