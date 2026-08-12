#ifndef EOS_FD_TABLE_H
#define EOS_FD_TABLE_H

#include <stdint.h>

#define EOS_FD_TABLE_CAPACITY UINT32_C(64)
#define EOS_FD_FLAG_CLOEXEC UINT32_C(1)

typedef enum eos_fd_kind {
    EOS_FD_KIND_NONE = 0,
    EOS_FD_KIND_CONSOLE = 1,
    EOS_FD_KIND_FILE = 2,
    EOS_FD_KIND_DIRECTORY = 3,
    EOS_FD_KIND_PIPE_READER = 4,
    EOS_FD_KIND_PIPE_WRITER = 5,
    EOS_FD_KIND_SOCKET = 6,
} eos_fd_kind;

typedef union eos_fd_native {
    void *pointer;
    uintptr_t word;
    struct {
        void *object;
        uint32_t value;
    } composite;
} eos_fd_native;

typedef struct eos_fd_token {
    int32_t descriptor;
    uint32_t generation;
} eos_fd_token;

typedef struct eos_fd_reference {
    uint32_t slot_index;
    uint32_t slot_generation;
    uint64_t lease_id;
    uint32_t active;
    eos_fd_kind kind;
    eos_fd_native native;
} eos_fd_reference;

#ifdef EOS_RUST_HOST_TEST
#  ifdef __cplusplus
extern "C" {
#  endif

void eos_fd_test_reset(void);
uint32_t eos_fd_test_capacity(void);
int32_t eos_fd_test_allocate(eos_fd_kind kind,
                             uint64_t native_identity,
                             uint32_t descriptor_flags);
int32_t eos_fd_test_close(int32_t descriptor);
int32_t eos_fd_test_dup(int32_t descriptor);
int32_t eos_fd_test_dup2(int32_t old_descriptor, int32_t new_descriptor);
int32_t eos_fd_test_get_flags(int32_t descriptor, uint32_t *flags);
int32_t eos_fd_test_set_cloexec(int32_t descriptor, int32_t enabled);
int32_t eos_fd_test_get_token(int32_t descriptor, eos_fd_token *token);
int32_t eos_fd_test_acquire(int32_t descriptor,
                            eos_fd_kind required_kind,
                            eos_fd_reference *reference);
int32_t eos_fd_test_acquire_token(eos_fd_token token,
                                  eos_fd_kind required_kind,
                                  eos_fd_reference *reference);
int32_t eos_fd_test_reference_identity(const eos_fd_reference *reference,
                                       uint64_t *native_identity);
int32_t eos_fd_test_release(eos_fd_reference *reference);
uint32_t eos_fd_test_destructor_count(uint64_t native_identity);
int32_t eos_fd_test_is_kind(int32_t descriptor, eos_fd_kind kind);
void eos_fd_test_exhaust_lease_ids_after_next_acquire(void);

#  ifdef __cplusplus
}
#  endif
#endif

#endif
