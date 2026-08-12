#include "eos_tls.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct eos_tls_value {
    eos_rust_tls_key_t key;
    void *value;
    struct eos_tls_value *next;
} eos_tls_value;

typedef struct eos_tls_root {
    int32_t error_number;
    uint32_t thread_identity;
    uint32_t destructor_pass;
    uint32_t cleanup_active;
    eos_tls_value *values;
    uintptr_t runtime_words[4];
} eos_tls_root;

typedef struct eos_tls_key_record {
    eos_rust_tls_key_t identity;
    uint32_t active;
    uint32_t callback_references;
    void (*destructor)(void *);
    struct eos_tls_key_record *next;
} eos_tls_key_record;

static eos_tls_key_record *eos_tls_keys;
static uint32_t eos_tls_next_key = UINT32_C(1);
static uint32_t eos_tls_key_exhausted;

#ifdef EOS_RUST_HOST_TEST
static void (*eos_tls_before_destructor_hook)(void *);
static void *eos_tls_before_destructor_context;

void eos_tls_test_set_before_destructor_hook(void (*hook)(void *),
                                             void *context) {
    eos_tls_before_destructor_hook = hook;
    eos_tls_before_destructor_context = context;
}
#endif

static int32_t eos_tls_status_error(int32_t status) {
    eos_error_result mapped = eos_error_from_port_status_impl(status, "tls");
    return mapped.kind == EOS_ERROR_ERRNO ? mapped.error_number : EOS_ERRNO_IO;
}

static void eos_tls_abort_message(const char *message) {
    eos_port_direct_diagnostic(message);
    eos_rust_abort();
}

static void eos_tls_lock(void) {
    if (eos_port_lock_acquire(EOS_PORT_LOCK_TLS_KEYS) != 0) {
        eos_tls_abort_message("libeos_rust_abi: TLS key lock acquisition failed\n");
    }
}

static void eos_tls_unlock(void) {
    if (eos_port_lock_release(EOS_PORT_LOCK_TLS_KEYS) != 0) {
        eos_tls_abort_message("libeos_rust_abi: TLS key lock release failed\n");
    }
}

static eos_tls_root *eos_tls_root_current(void) {
    uintptr_t value = 0;
    eos_tls_root *root;
    int32_t status = eos_port_thread_tls_get(EOS_RUST_TLS_SLOT, &value);
    if (status != 0) {
        eos_tls_abort_message("libeos_rust_abi: reserved TLS slot read failed\n");
    }
    if (value != 0) return (eos_tls_root *)value;

    root = NULL;
    status = eos_port_memory_alloc((uint32_t)sizeof(*root), (void **)&root);
    if (status != 0 || root == NULL) {
        eos_tls_abort_message("libeos_rust_abi: TLS root allocation failed\n");
    }
    (void)memset(root, 0, sizeof(*root));
    status = eos_port_thread_tls_set(EOS_RUST_TLS_SLOT, (uintptr_t)root);
    if (status != 0) {
        if (eos_port_memory_free(root) != 0) {
            eos_tls_abort_message("libeos_rust_abi: TLS root rollback failed\n");
        }
        eos_tls_abort_message("libeos_rust_abi: reserved TLS slot install failed\n");
    }
    return root;
}

static int32_t *eos_tls_errno_location(void) {
    return &eos_tls_root_current()->error_number;
}

static void eos_tls_set_thread_identity(uint32_t identity) {
    eos_tls_root_current()->thread_identity = identity;
}

static uint32_t eos_tls_thread_identity(void) {
    return eos_tls_root_current()->thread_identity;
}

static eos_tls_key_record *eos_tls_find_key_locked(eos_rust_tls_key_t key) {
    eos_tls_key_record *record = eos_tls_keys;
    while (record != NULL && record->identity != key) record = record->next;
    return record;
}

static eos_tls_value *eos_tls_find_value(eos_tls_root *root,
                                         eos_rust_tls_key_t key) {
    eos_tls_value *value = root->values;
    while (value != NULL && value->key != key) value = value->next;
    return value;
}

int32_t eos_rust_pthread_key_create(eos_rust_tls_key_t *key,
                                    void (*destructor)(void *)) {
    eos_tls_key_record *record = NULL;
    int32_t status;
    if (key == NULL) return EOS_ERRNO_INVALID;
    *key = 0;
    status = eos_port_memory_alloc((uint32_t)sizeof(*record), (void **)&record);
    if (status != 0 || record == NULL) return eos_tls_status_error(status);
    (void)memset(record, 0, sizeof(*record));

    status = eos_port_lock_acquire(EOS_PORT_LOCK_TLS_KEYS);
    if (status != 0) {
        if (eos_port_memory_free(record) != 0) eos_rust_abort();
        return eos_tls_status_error(status);
    }
    if (eos_tls_key_exhausted != 0) {
        eos_tls_unlock();
        if (eos_port_memory_free(record) != 0) eos_rust_abort();
        return EOS_ERRNO_WOULD_BLOCK;
    }
    record->identity = eos_tls_next_key;
    record->active = UINT32_C(1);
    record->destructor = destructor;
    record->next = eos_tls_keys;
    eos_tls_keys = record;
    if (eos_tls_next_key == UINT32_MAX) {
        eos_tls_key_exhausted = UINT32_C(1);
    } else {
        ++eos_tls_next_key;
    }
    *key = record->identity;
    eos_tls_unlock();
    return 0;
}

int32_t eos_rust_pthread_key_delete(eos_rust_tls_key_t key) {
    eos_tls_key_record *record;
    int32_t status = eos_port_lock_acquire(EOS_PORT_LOCK_TLS_KEYS);
    if (status != 0) return eos_tls_status_error(status);
    record = eos_tls_find_key_locked(key);
    if (record == NULL || record->active == 0) {
        eos_tls_unlock();
        return EOS_ERRNO_INVALID;
    }
    record->active = 0;
    if (record->callback_references == 0) record->destructor = NULL;
    eos_tls_unlock();
    return 0;
}

void *eos_rust_pthread_getspecific(eos_rust_tls_key_t key) {
    eos_tls_key_record *record;
    eos_tls_value *value;
    eos_tls_root *root = eos_tls_root_current();
    int32_t status = eos_port_lock_acquire(EOS_PORT_LOCK_TLS_KEYS);
    if (status != 0) {
        root->error_number = eos_tls_status_error(status);
        return NULL;
    }
    record = eos_tls_find_key_locked(key);
    if (record == NULL || record->active == 0) {
        eos_tls_unlock();
        root->error_number = EOS_ERRNO_INVALID;
        return NULL;
    }
    value = eos_tls_find_value(root, key);
    {
        void *result = value == NULL ? NULL : value->value;
        eos_tls_unlock();
        return result;
    }
}

int32_t eos_rust_pthread_setspecific(eos_rust_tls_key_t key,
                                     const void *value) {
    eos_tls_key_record *record;
    eos_tls_value *entry;
    eos_tls_value *created = NULL;
    eos_tls_root *root = eos_tls_root_current();
    int32_t status;

    eos_tls_lock();
    record = eos_tls_find_key_locked(key);
    if (record == NULL || record->active == 0) {
        eos_tls_unlock();
        return EOS_ERRNO_INVALID;
    }
    entry = eos_tls_find_value(root, key);
    if (entry != NULL) {
        entry->value = (void *)value;
        eos_tls_unlock();
        return 0;
    }
    eos_tls_unlock();
    if (value == NULL) return 0;
    status = eos_port_memory_alloc((uint32_t)sizeof(*created), (void **)&created);
    if (status != 0 || created == NULL) return eos_tls_status_error(status);
    created->key = key;
    created->value = (void *)value;

    eos_tls_lock();
    record = eos_tls_find_key_locked(key);
    if (record == NULL || record->active == 0) {
        eos_tls_unlock();
        if (eos_port_memory_free(created) != 0) eos_rust_abort();
        return EOS_ERRNO_INVALID;
    }
    entry = eos_tls_find_value(root, key);
    if (entry != NULL) {
        entry->value = (void *)value;
        eos_tls_unlock();
        if (eos_port_memory_free(created) != 0) eos_rust_abort();
        return 0;
    }
    created->next = root->values;
    root->values = created;
    eos_tls_unlock();
    return 0;
}

static int eos_tls_next_destructor(eos_tls_root *root,
                                   eos_rust_tls_key_t after,
                                   eos_rust_tls_key_t pass_limit,
                                   eos_rust_tls_key_t *identity,
                                   void (**destructor)(void *),
                                   void **argument,
                                   eos_tls_key_record **ownership) {
    eos_tls_value *value;
    eos_rust_tls_key_t selected = UINT32_MAX;
    void (*selected_destructor)(void *) = NULL;
    eos_tls_value *selected_value = NULL;
    eos_tls_key_record *selected_key = NULL;

    eos_tls_lock();
    for (value = root->values; value != NULL; value = value->next) {
        eos_tls_key_record *key;
        if (value->value == NULL || value->key <= after ||
            value->key > pass_limit || value->key >= selected) {
            continue;
        }
        key = eos_tls_find_key_locked(value->key);
        if (key != NULL && key->active != 0 && key->destructor != NULL) {
            selected = value->key;
            selected_destructor = key->destructor;
            selected_value = value;
            selected_key = key;
        }
    }
    if (selected_value != NULL) {
        ++selected_key->callback_references;
        *argument = selected_value->value;
        selected_value->value = NULL;
        *identity = selected;
        *destructor = selected_destructor;
        *ownership = selected_key;
    }
    eos_tls_unlock();
    return selected_value != NULL;
}

static void eos_tls_release_destructor(eos_tls_key_record *ownership) {
    eos_tls_lock();
    if (ownership->callback_references == 0) eos_rust_abort();
    --ownership->callback_references;
    if (ownership->active == 0 && ownership->callback_references == 0) {
        ownership->destructor = NULL;
    }
    eos_tls_unlock();
}

static void eos_tls_cleanup_current(void) {
    eos_tls_root *root;
    uintptr_t value = 0;
    int32_t status = eos_port_thread_tls_get(EOS_RUST_TLS_SLOT, &value);
    uint32_t pass;
    if (status != 0) eos_tls_abort_message("libeos_rust_abi: TLS cleanup read failed\n");
    if (value == 0) return;
    root = (eos_tls_root *)value;
    root->cleanup_active = UINT32_C(1);

    for (pass = 0; pass < EOS_RUST_TLS_DESTRUCTOR_PASSES; ++pass) {
        eos_rust_tls_key_t cursor = 0;
        eos_rust_tls_key_t pass_limit;
        eos_rust_tls_key_t identity = 0;
        void (*destructor)(void *) = NULL;
        void *argument = NULL;
        eos_tls_key_record *ownership = NULL;
        root->destructor_pass = pass + UINT32_C(1);
        eos_tls_lock();
        pass_limit = eos_tls_next_key - (eos_tls_key_exhausted == 0 ? 1U : 0U);
        eos_tls_unlock();
        while (eos_tls_next_destructor(root, cursor, pass_limit, &identity,
                                       &destructor, &argument, &ownership)) {
            cursor = identity;
#ifdef EOS_RUST_HOST_TEST
            if (eos_tls_before_destructor_hook != NULL) {
                eos_tls_before_destructor_hook(
                    eos_tls_before_destructor_context);
            }
#endif
            destructor(argument);
            eos_tls_release_destructor(ownership);
        }
    }

    while (root->values != NULL) {
        eos_tls_value *dead = root->values;
        root->values = dead->next;
        if (eos_port_memory_free(dead) != 0) {
            eos_tls_abort_message("libeos_rust_abi: TLS value free failed\n");
        }
    }
    status = eos_port_thread_tls_set(EOS_RUST_TLS_SLOT, (uintptr_t)0);
    if (status != 0) eos_tls_abort_message("libeos_rust_abi: TLS cleanup clear failed\n");
    if (eos_port_memory_free(root) != 0) {
        eos_tls_abort_message("libeos_rust_abi: TLS root free failed\n");
    }
}
