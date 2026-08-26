#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <poll.h>
#include <sched.h>

/* glibc exposes this as a macro; do not let it rewrite the stable ABI field. */
#ifdef s6_addr
#undef s6_addr
#endif

static pthread_key_t eos_host_tls_slot_keys[8];
static pthread_once_t eos_host_tls_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t eos_host_locks[EOS_PORT_LOCK_COUNT] = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER};
static pthread_mutex_t eos_host_console_guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t eos_host_process_guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t eos_host_process_condition = PTHREAD_COND_INITIALIZER;
#define EOS_HOST_PROCESS_CAPACITY UINT32_C(64)
typedef struct eos_host_process_record {
    uint32_t in_use;
    uint32_t started;
    uint32_t active;
    uint32_t release;
    uint32_t killed;
    uint64_t sequence;
    char name[64];
    char program[256];
    char argv[16][128];
    char envp[16][128];
    char cwd[256];
    int32_t inherited[64];
    uint32_t argc;
    uint32_t envc;
    uint32_t inherited_count;
} eos_host_process_record;
static eos_host_process_record
    eos_host_process_records[EOS_HOST_PROCESS_CAPACITY];
static uint64_t eos_host_process_next_sequence = UINT64_C(1);
static eos_host_process_record *eos_host_process_find_locked(
    const char *name, int require_live) {
    uint32_t index;
    for (index = 0; index < EOS_HOST_PROCESS_CAPACITY; ++index) {
        eos_host_process_record *record = &eos_host_process_records[index];
        if (record->name[0] != '\0' && strcmp(record->name, name) == 0 &&
            (!require_live || record->in_use != UINT32_C(0))) {
            return record;
        }
    }
    return NULL;
}
#ifdef EOS_RUST_HOST_TEST
static pthread_mutex_t eos_host_closedir_guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t eos_host_closedir_condition = PTHREAD_COND_INITIALIZER;
static int eos_host_closedir_pause;
static int eos_host_closedir_entered;
static int eos_host_closedir_resume;
#endif
static char eos_host_console_in[4096];
static uint32_t eos_host_console_in_length;
static uint32_t eos_host_console_in_offset;
static char eos_host_console_out[2][4096];
static uint32_t eos_host_console_out_length[2];
static char eos_host_hostname[64] = "eos-host";

#ifdef EOS_RUST_HOST_TEST
static int32_t eos_host_next_alloc_status;
static int32_t eos_host_next_aligned_status;
static int32_t eos_host_next_realloc_status;
static int32_t eos_host_next_lock_status;
static uint32_t eos_host_lock_successes_before_failure;
static int32_t eos_host_delayed_lock_status;
static int32_t eos_host_next_unlock_status;
static int32_t eos_host_next_environment_set_status;
static int32_t eos_host_next_environment_unset_status;
static int eos_host_native_environment_present;
static char eos_host_native_environment_name[64];
static char eos_host_native_environment_value[EOS_PORT_ENV_VALUE_CAPACITY];
static int32_t eos_host_console_failure[3];
static _Atomic uint32_t eos_host_console_open_count[3];
static uint32_t eos_host_console_partial_bytes[3];
static int32_t eos_host_console_partial_status[3];
static uint32_t eos_host_file_partial_read_bytes;
static int32_t eos_host_file_partial_read_status;
static uint32_t eos_host_file_partial_write_bytes;
static int32_t eos_host_file_partial_write_status;
static int32_t eos_host_next_file_close_status;
static int32_t eos_host_next_sync_wait_status;
static int32_t eos_host_next_sync_create_status;
static int32_t eos_host_next_process_kill_status;
static int32_t eos_host_next_process_unload_status;
static int32_t eos_host_process_kill_after_match_status;
static _Atomic uint32_t eos_host_process_test_capabilities;
static uint32_t eos_host_seek_successes_before_failure;
static int32_t eos_host_delayed_seek_status;
static int32_t eos_host_next_thread_create_status;
static int32_t eos_host_next_tls_get_status;
static int32_t eos_host_next_tls_set_status;
static _Atomic uint32_t eos_host_last_tls_get_slot;
static _Atomic uint32_t eos_host_last_tls_set_slot;
static _Atomic uint32_t eos_host_last_thread_stack;
static _Atomic uint32_t eos_host_last_thread_features;
static _Atomic uint32_t eos_host_last_thread_priority;
static _Atomic uint32_t eos_host_native_thread_delete_count;
static _Atomic uint32_t eos_host_thread_completion_count;
static int eos_host_finish_thread_before_create_returns;
static int32_t eos_host_start_thread_then_fail_status;
static int32_t eos_host_next_sync_destroy_status;
static int32_t eos_host_next_free_status;
static uint32_t eos_host_tls_set_successes_before_failure;
static int32_t eos_host_delayed_tls_set_status;
static _Atomic uint32_t eos_host_sync_create_count;
static _Atomic uint32_t eos_host_sync_destroy_count;
static int32_t eos_host_next_public_mutex_create_status;
static int32_t eos_host_next_public_mutex_lock_status;
static int32_t eos_host_next_public_mutex_unlock_status;
static int32_t eos_host_next_public_mutex_delete_status;
static int32_t eos_host_next_public_sem_create_status;
static int32_t eos_host_next_public_sem_take_status;
static int eos_host_next_public_sem_spurious;
static _Atomic uint32_t eos_host_public_sem_take_failure_pause;
static _Atomic uint32_t eos_host_public_sem_take_failure_entered;
static _Atomic uint32_t eos_host_public_sem_take_failure_release;
static int32_t eos_host_next_public_sem_give_status;
static int32_t eos_host_next_public_sem_delete_status;
static pthread_mutex_t eos_host_time_guard = PTHREAD_MUTEX_INITIALIZER;
#define EOS_HOST_TIME_SCRIPT_CAPACITY UINT32_C(32000)
#define EOS_HOST_TIME_DELAY_CAPACITY UINT32_C(32)
static uint64_t eos_host_time_script[EOS_HOST_TIME_SCRIPT_CAPACITY];
static uint32_t eos_host_time_script_count;
static uint32_t eos_host_time_script_index;
static uint64_t eos_host_time_fake_now;
static int eos_host_time_fake;
static int eos_host_time_delay_advances;
static uint64_t eos_host_time_realtime;
static int32_t eos_host_time_realtime_status;
static uint32_t eos_host_time_delay_values[EOS_HOST_TIME_DELAY_CAPACITY];
static uint32_t eos_host_time_delay_count;
static uint32_t eos_host_time_delay_usec_values[EOS_HOST_TIME_DELAY_CAPACITY];
static uint32_t eos_host_time_delay_usec_count;
static uint32_t eos_host_time_delay_successes_before_failure;
static int32_t eos_host_time_delayed_failure;
static uint32_t eos_host_alloc_successes_before_failure;
static int32_t eos_host_delayed_alloc_status;
static int32_t eos_host_next_socket_create_error;
static int32_t eos_host_next_socket_connect_error;
static int32_t eos_host_next_socket_close_error;
static _Atomic uint32_t eos_host_socket_close_count;
static uint32_t eos_host_socket_partial_send_bytes;
static int32_t eos_host_socket_partial_send_error;
static uint32_t eos_host_socket_partial_receive_bytes;
static int32_t eos_host_socket_partial_receive_error;
static uint32_t eos_host_timeout_successes_before_failure;
static int32_t eos_host_delayed_timeout_error;
#define EOS_HOST_TIMEOUT_LOG_CAPACITY UINT32_C(16)
static uint32_t eos_host_timeout_log_count;
static uint32_t eos_host_timeout_log_receive[EOS_HOST_TIMEOUT_LOG_CAPACITY];
static uint32_t eos_host_timeout_log_ticks[EOS_HOST_TIMEOUT_LOG_CAPACITY];
static _Atomic uint32_t eos_host_poll_pause;
static _Atomic uint32_t eos_host_poll_entered;
static _Atomic uint32_t eos_host_poll_release;
static _Atomic uint32_t eos_host_poll_forced_events;

void eos_host_test_reset(void) {
    eos_host_next_alloc_status = 0;
    eos_host_next_aligned_status = 0;
    eos_host_next_realloc_status = 0;
    eos_host_next_lock_status = 0;
    eos_host_lock_successes_before_failure = UINT32_MAX;
    eos_host_delayed_lock_status = 0;
    eos_host_next_unlock_status = 0;
    eos_host_next_environment_set_status = 0;
    eos_host_next_environment_unset_status = 0;
    eos_host_native_environment_present = 0;
    for (uint32_t stream = 0; stream < UINT32_C(3); ++stream) {
        eos_host_console_failure[stream] = 0;
        atomic_store_explicit(&eos_host_console_open_count[stream],
                              UINT32_C(0), memory_order_relaxed);
        eos_host_console_partial_bytes[stream] = UINT32_MAX;
        eos_host_console_partial_status[stream] = 0;
    }
    (void)pthread_mutex_lock(&eos_host_console_guard);
    eos_host_console_in_length = 0;
    eos_host_console_in_offset = 0;
    eos_host_console_out_length[0] = 0;
    eos_host_console_out_length[1] = 0;
    eos_host_file_partial_read_bytes = UINT32_MAX;
    eos_host_file_partial_read_status = 0;
    eos_host_file_partial_write_bytes = UINT32_MAX;
    eos_host_file_partial_write_status = 0;
    eos_host_next_file_close_status = 0;
    eos_host_next_sync_wait_status = 0;
    eos_host_next_sync_create_status = 0;
    eos_host_next_process_kill_status = 0;
    eos_host_next_process_unload_status = 0;
    eos_host_process_kill_after_match_status = 0;
    atomic_store(&eos_host_process_test_capabilities,
                 EOS_PORT_PROCESS_CAP_ENVIRONMENT |
                     EOS_PORT_PROCESS_CAP_CWD |
                     EOS_PORT_PROCESS_CAP_STDERR |
                     EOS_PORT_PROCESS_CAP_DESCRIPTOR_INHERITANCE);
    eos_host_seek_successes_before_failure = UINT32_MAX;
    eos_host_delayed_seek_status = 0;
    eos_host_next_thread_create_status = 0;
    eos_host_next_tls_get_status = 0;
    eos_host_next_tls_set_status = 0;
    atomic_store(&eos_host_last_tls_get_slot, UINT32_MAX);
    atomic_store(&eos_host_last_tls_set_slot, UINT32_MAX);
    atomic_store(&eos_host_last_thread_stack, 0);
    atomic_store(&eos_host_last_thread_features, UINT32_MAX);
    atomic_store(&eos_host_last_thread_priority, 0);
    atomic_store(&eos_host_native_thread_delete_count, 0);
    atomic_store(&eos_host_thread_completion_count, 0);
    eos_host_finish_thread_before_create_returns = 0;
    eos_host_start_thread_then_fail_status = 0;
    eos_host_next_sync_destroy_status = 0;
    eos_host_next_free_status = 0;
    eos_host_tls_set_successes_before_failure = UINT32_MAX;
    eos_host_delayed_tls_set_status = 0;
    atomic_store(&eos_host_sync_create_count, 0);
    atomic_store(&eos_host_sync_destroy_count, 0);
    eos_host_next_public_mutex_create_status = 0;
    eos_host_next_public_mutex_lock_status = 0;
    eos_host_next_public_mutex_unlock_status = 0;
    eos_host_next_public_mutex_delete_status = 0;
    eos_host_next_public_sem_create_status = 0;
    eos_host_next_public_sem_take_status = 0;
    eos_host_next_public_sem_spurious = 0;
    atomic_store(&eos_host_public_sem_take_failure_pause, UINT32_C(0));
    atomic_store(&eos_host_public_sem_take_failure_entered, UINT32_C(0));
    atomic_store(&eos_host_public_sem_take_failure_release, UINT32_C(1));
    eos_host_next_public_sem_give_status = 0;
    eos_host_next_public_sem_delete_status = 0;
    eos_host_alloc_successes_before_failure = UINT32_MAX;
    eos_host_delayed_alloc_status = 0;
    eos_host_next_socket_create_error = 0;
    eos_host_next_socket_connect_error = 0;
    eos_host_next_socket_close_error = 0;
    atomic_store(&eos_host_socket_close_count, 0);
    eos_host_socket_partial_send_bytes = UINT32_MAX;
    eos_host_socket_partial_send_error = 0;
    eos_host_socket_partial_receive_bytes = UINT32_MAX;
    eos_host_socket_partial_receive_error = 0;
    eos_host_timeout_successes_before_failure = UINT32_MAX;
    eos_host_delayed_timeout_error = 0;
    eos_host_timeout_log_count = 0;
    atomic_store(&eos_host_poll_pause, 0);
    atomic_store(&eos_host_poll_entered, 0);
    atomic_store(&eos_host_poll_release, 1);
    atomic_store(&eos_host_poll_forced_events, 0);
    (void)strcpy(eos_host_hostname, "eos-host");
    (void)pthread_mutex_unlock(&eos_host_console_guard);
    (void)pthread_mutex_lock(&eos_host_closedir_guard);
    eos_host_closedir_pause = 0;
    eos_host_closedir_entered = 0;
    eos_host_closedir_resume = 1;
    (void)pthread_cond_broadcast(&eos_host_closedir_condition);
    (void)pthread_mutex_unlock(&eos_host_closedir_guard);
    (void)pthread_mutex_lock(&eos_host_process_guard);
    (void)memset(eos_host_process_records, 0,
                 sizeof(eos_host_process_records));
    eos_host_process_next_sequence = UINT64_C(1);
    (void)pthread_cond_broadcast(&eos_host_process_condition);
    (void)pthread_mutex_unlock(&eos_host_process_guard);
}
void eos_host_test_fail_next_alloc(int32_t status) { eos_host_next_alloc_status = status; }
void eos_host_test_fail_alloc_after(uint32_t successful_allocations,
                                    int32_t status) {
    eos_host_alloc_successes_before_failure = successful_allocations;
    eos_host_delayed_alloc_status = status;
}
void eos_host_test_fail_next_aligned_alloc(int32_t status) { eos_host_next_aligned_status = status; }
void eos_host_test_fail_next_realloc(int32_t status) { eos_host_next_realloc_status = status; }
void eos_host_test_fail_next_lock(int32_t status) { eos_host_next_lock_status = status; }
void eos_host_test_fail_lock_after(uint32_t successful_locks, int32_t status) {
    eos_host_lock_successes_before_failure = successful_locks;
    eos_host_delayed_lock_status = status;
}
void eos_host_test_fail_next_unlock(int32_t status) { eos_host_next_unlock_status = status; }
void eos_host_test_fail_next_environment_set(int32_t status) { eos_host_next_environment_set_status = status; }
void eos_host_test_fail_next_environment_unset(int32_t status) { eos_host_next_environment_unset_status = status; }
void eos_host_test_fail_console(uint32_t stream, int32_t status) {
    if (stream < UINT32_C(3)) eos_host_console_failure[stream] = status;
}
uint32_t eos_host_test_console_open_count(uint32_t stream) {
    if (stream >= UINT32_C(3)) return UINT32_C(0);
    return atomic_load_explicit(&eos_host_console_open_count[stream],
                                memory_order_relaxed);
}
void eos_host_test_native_environment(const char *name, const char *value) {
    (void)strncpy(eos_host_native_environment_name, name,
                  sizeof(eos_host_native_environment_name) - 1U);
    eos_host_native_environment_name[sizeof(eos_host_native_environment_name) - 1U] = '\0';
    (void)strncpy(eos_host_native_environment_value, value,
                  sizeof(eos_host_native_environment_value) - 1U);
    eos_host_native_environment_value[sizeof(eos_host_native_environment_value) - 1U] = '\0';
    eos_host_native_environment_present = 1;
}
void eos_host_test_file_partial_read(uint32_t bytes, int32_t status) {
    eos_host_file_partial_read_bytes = bytes;
    eos_host_file_partial_read_status = status;
}
void eos_host_test_file_partial_write(uint32_t bytes, int32_t status) {
    eos_host_file_partial_write_bytes = bytes;
    eos_host_file_partial_write_status = status;
}
void eos_host_test_fail_next_file_close(int32_t status) {
    eos_host_next_file_close_status = status;
}
void eos_host_test_fail_next_sync_wait(int32_t status) {
    eos_host_next_sync_wait_status = status;
}
void eos_host_test_fail_next_sync_create(int32_t status) {
    eos_host_next_sync_create_status = status;
}
void eos_host_test_fail_next_process_kill(int32_t status) {
    eos_host_next_process_kill_status = status;
}
void eos_host_test_fail_process_kill_after_match(int32_t status) {
    (void)pthread_mutex_lock(&eos_host_process_guard);
    eos_host_process_kill_after_match_status = status;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
}
void eos_host_test_fail_next_process_unload(int32_t status) {
    eos_host_next_process_unload_status = status;
}
void eos_host_test_use_martos_process_capabilities(void) {
    atomic_store(&eos_host_process_test_capabilities,
                 EOS_PORT_PROCESS_CAP_NATIVE_STDERR);
}
void eos_host_test_fail_seek_after(uint32_t successful_seeks, int32_t status) {
    eos_host_seek_successes_before_failure = successful_seeks;
    eos_host_delayed_seek_status = status;
}
void eos_host_test_fail_next_thread_create(int32_t status) {
    eos_host_next_thread_create_status = status;
}
void eos_host_test_fail_next_tls_get(int32_t status) {
    eos_host_next_tls_get_status = status;
}
void eos_host_test_fail_next_tls_set(int32_t status) {
    eos_host_next_tls_set_status = status;
}
void eos_host_test_fail_tls_set_after(uint32_t successful_sets, int32_t status) {
    eos_host_tls_set_successes_before_failure = successful_sets;
    eos_host_delayed_tls_set_status = status;
}
void eos_host_test_finish_thread_before_create_returns(int enabled) {
    eos_host_finish_thread_before_create_returns = enabled;
}
void eos_host_test_start_thread_then_fail(int32_t status) {
    eos_host_start_thread_then_fail_status = status;
}
void eos_host_test_fail_next_sync_destroy(int32_t status) {
    eos_host_next_sync_destroy_status = status;
}
void eos_host_test_fail_next_free(int32_t status) {
    eos_host_next_free_status = status;
}
void eos_host_test_fail_next_public_mutex_create(int32_t status) {
    eos_host_next_public_mutex_create_status = status;
}
void eos_host_test_fail_next_public_mutex_lock(int32_t status) {
    eos_host_next_public_mutex_lock_status = status;
}
void eos_host_test_fail_next_public_mutex_unlock(int32_t status) {
    eos_host_next_public_mutex_unlock_status = status;
}
void eos_host_test_fail_next_public_mutex_delete(int32_t status) {
    eos_host_next_public_mutex_delete_status = status;
}
void eos_host_test_fail_next_public_sem_create(int32_t status) {
    eos_host_next_public_sem_create_status = status;
}
void eos_host_test_fail_next_public_sem_take(int32_t status) {
    eos_host_next_public_sem_take_status = status;
}
void eos_host_test_spurious_next_public_sem_take(void) {
    eos_host_next_public_sem_spurious = 1;
}
void eos_host_test_pause_public_sem_take_failure(int enabled) {
    atomic_store(&eos_host_public_sem_take_failure_entered, UINT32_C(0));
    atomic_store(&eos_host_public_sem_take_failure_release,
                 enabled ? UINT32_C(0) : UINT32_C(1));
    atomic_store(&eos_host_public_sem_take_failure_pause,
                 enabled ? UINT32_C(1) : UINT32_C(0));
}
uint32_t eos_host_test_public_sem_take_failure_pause_entered(void) {
    return atomic_load(&eos_host_public_sem_take_failure_entered);
}
void eos_host_test_resume_public_sem_take_failure(void) {
    atomic_store(&eos_host_public_sem_take_failure_release, UINT32_C(1));
    atomic_store(&eos_host_public_sem_take_failure_pause, UINT32_C(0));
}
void eos_host_test_fail_next_public_sem_give(int32_t status) {
    eos_host_next_public_sem_give_status = status;
}
void eos_host_test_fail_next_public_sem_delete(int32_t status) {
    eos_host_next_public_sem_delete_status = status;
}
uint32_t eos_host_test_sync_create_count(void) {
    return atomic_load(&eos_host_sync_create_count);
}
uint32_t eos_host_test_sync_destroy_count(void) {
    return atomic_load(&eos_host_sync_destroy_count);
}
uint32_t eos_host_test_last_tls_get_slot(void) {
    return atomic_load(&eos_host_last_tls_get_slot);
}
uint32_t eos_host_test_last_tls_set_slot(void) {
    return atomic_load(&eos_host_last_tls_set_slot);
}
uint32_t eos_host_test_last_thread_stack(void) {
    return atomic_load(&eos_host_last_thread_stack);
}
uint32_t eos_host_test_last_thread_features(void) {
    return atomic_load(&eos_host_last_thread_features);
}
uint32_t eos_host_test_last_thread_priority(void) {
    return atomic_load(&eos_host_last_thread_priority);
}
uint32_t eos_host_test_native_thread_delete_count(void) {
    return atomic_load(&eos_host_native_thread_delete_count);
}
void eos_host_test_fail_next_socket_create(int32_t error_number) {
    eos_host_next_socket_create_error = error_number;
}
void eos_host_test_fail_next_socket_connect(int32_t error_number) {
    eos_host_next_socket_connect_error = error_number;
}
void eos_host_test_fail_next_socket_close(int32_t error_number) {
    eos_host_next_socket_close_error = error_number;
}
uint32_t eos_host_test_socket_close_count(void) {
    return atomic_load(&eos_host_socket_close_count);
}
void eos_host_test_socket_partial_send(uint32_t bytes, int32_t error_number) {
    eos_host_socket_partial_send_bytes = bytes;
    eos_host_socket_partial_send_error = error_number;
}
void eos_host_test_socket_partial_receive(uint32_t bytes,
                                          int32_t error_number) {
    eos_host_socket_partial_receive_bytes = bytes;
    eos_host_socket_partial_receive_error = error_number;
}
void eos_host_test_fail_socket_timeout_after(uint32_t successful_updates,
                                             int32_t error_number) {
    eos_host_timeout_successes_before_failure = successful_updates;
    eos_host_delayed_timeout_error = error_number;
}
uint32_t eos_host_test_socket_timeout_log_count(void) {
    return eos_host_timeout_log_count;
}
uint32_t eos_host_test_socket_timeout_log_receive(uint32_t index) {
    return index < eos_host_timeout_log_count
               ? eos_host_timeout_log_receive[index] : UINT32_MAX;
}
uint32_t eos_host_test_socket_timeout_log_ticks(uint32_t index) {
    return index < eos_host_timeout_log_count
               ? eos_host_timeout_log_ticks[index] : UINT32_MAX;
}
void eos_host_test_pause_socket_poll(int enabled) {
    atomic_store(&eos_host_poll_entered, 0);
    atomic_store(&eos_host_poll_release, enabled ? 0U : 1U);
    atomic_store(&eos_host_poll_pause, enabled ? 1U : 0U);
}
uint32_t eos_host_test_socket_poll_entered(void) {
    return atomic_load(&eos_host_poll_entered);
}
void eos_host_test_resume_socket_poll(void) {
    atomic_store(&eos_host_poll_release, 1);
    atomic_store(&eos_host_poll_pause, 0);
}
void eos_host_test_force_socket_poll_events(uint32_t compatibility_events) {
    uint32_t port_events = 0;
    if ((compatibility_events & EOS_RUST_POLLIN) != 0) {
        port_events |= EOS_PORT_SOCKET_EVENT_READ;
    }
    if ((compatibility_events & EOS_RUST_POLLOUT) != 0) {
        port_events |= EOS_PORT_SOCKET_EVENT_WRITE;
    }
    if ((compatibility_events & EOS_RUST_POLLERR) != 0) {
        port_events |= EOS_PORT_SOCKET_EVENT_ERROR;
    }
    if ((compatibility_events & EOS_RUST_POLLHUP) != 0) {
        port_events |= EOS_PORT_SOCKET_EVENT_HANGUP;
    }
    if ((compatibility_events & EOS_RUST_POLLPRI) != 0) {
        port_events |= EOS_PORT_SOCKET_EVENT_PRIORITY;
    }
    atomic_store(&eos_host_poll_forced_events, port_events);
}
void eos_host_test_console_input(const char *text) {
    size_t length = strlen(text);
    if (length > sizeof(eos_host_console_in)) length = sizeof(eos_host_console_in);
    (void)pthread_mutex_lock(&eos_host_console_guard);
    (void)memcpy(eos_host_console_in, text, length);
    eos_host_console_in_length = (uint32_t)length;
    eos_host_console_in_offset = 0;
    (void)pthread_mutex_unlock(&eos_host_console_guard);
}
uint32_t eos_host_test_console_output(uint32_t stream, char *buffer,
                                      uint32_t capacity) {
    uint32_t index;
    uint32_t count;
    if (stream < UINT32_C(1) || stream > UINT32_C(2) || buffer == NULL) return 0;
    index = stream - UINT32_C(1);
    (void)pthread_mutex_lock(&eos_host_console_guard);
    count = eos_host_console_out_length[index] < capacity
                ? eos_host_console_out_length[index]
                : capacity;
    (void)memcpy(buffer, eos_host_console_out[index], count);
    (void)pthread_mutex_unlock(&eos_host_console_guard);
    return count;
}
void eos_host_test_console_partial(uint32_t stream, uint32_t bytes,
                                   int32_t status) {
    if (stream < UINT32_C(3)) {
        eos_host_console_partial_bytes[stream] = bytes;
        eos_host_console_partial_status[stream] = status;
    }
}
void eos_host_test_hostname(const char *name) {
    (void)pthread_mutex_lock(&eos_host_console_guard);
    (void)strncpy(eos_host_hostname, name, sizeof(eos_host_hostname) - 1U);
    eos_host_hostname[sizeof(eos_host_hostname) - 1U] = '\0';
    (void)pthread_mutex_unlock(&eos_host_console_guard);
}

static void eos_host_test_process_identity_name(uint32_t identity,
                                                char name[64]) {
    static const char digits[] = "0123456789abcdef";
    uint32_t index;
    (void)memcpy(name, "eos.rust.", 9U);
    for (index = 0; index < UINT32_C(8); ++index) {
        name[9U + index] = digits[(identity >> (28U - index * 4U)) & 0xfU];
    }
    name[17] = '\0';
}
static eos_host_process_record *eos_host_process_find_identity_locked(
    uint32_t identity) {
    char name[64];
    eos_host_test_process_identity_name(identity, name);
    return eos_host_process_find_locked(name, 0);
}
static eos_host_process_record *eos_host_process_latest_locked(void) {
    eos_host_process_record *latest = NULL;
    uint32_t index;
    for (index = 0; index < EOS_HOST_PROCESS_CAPACITY; ++index) {
        eos_host_process_record *record = &eos_host_process_records[index];
        if (record->sequence != UINT64_C(0) &&
            (latest == NULL || record->sequence > latest->sequence)) {
            latest = record;
        }
    }
    return latest;
}
void eos_host_test_process_release(void) {
    uint32_t index;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    for (index = 0; index < EOS_HOST_PROCESS_CAPACITY; ++index) {
        eos_host_process_record *record = &eos_host_process_records[index];
        if (record->in_use != UINT32_C(0) &&
            record->active != UINT32_C(0)) {
            record->release = UINT32_C(1);
        }
    }
    (void)pthread_cond_broadcast(&eos_host_process_condition);
    (void)pthread_mutex_unlock(&eos_host_process_guard);
}
uint32_t eos_host_test_process_started(void) {
    eos_host_process_record *record;
    uint32_t value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_latest_locked();
    value = record == NULL ? UINT32_C(0) : record->started;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
uint32_t eos_host_test_process_active_count(void) {
    uint32_t count = 0;
    uint32_t index;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    for (index = 0; index < EOS_HOST_PROCESS_CAPACITY; ++index) {
        if (eos_host_process_records[index].active != UINT32_C(0)) ++count;
    }
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return count;
}
uint32_t eos_host_test_process_record_count(void) {
    uint32_t count = 0;
    uint32_t index;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    for (index = 0; index < EOS_HOST_PROCESS_CAPACITY; ++index) {
        if (eos_host_process_records[index].in_use != UINT32_C(0)) ++count;
    }
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return count;
}
uint32_t eos_host_test_process_active(void) {
    return eos_host_test_process_active_count();
}
uint32_t eos_host_test_process_argc(void) {
    eos_host_process_record *record;
    uint32_t value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_latest_locked();
    value = record == NULL ? UINT32_C(0) : record->argc;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
uint32_t eos_host_test_process_envc(void) {
    eos_host_process_record *record;
    uint32_t value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_latest_locked();
    value = record == NULL ? UINT32_C(0) : record->envc;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
uint32_t eos_host_test_process_inherited_count(void) {
    eos_host_process_record *record;
    uint32_t value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_latest_locked();
    value = record == NULL ? UINT32_C(0) : record->inherited_count;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
const char *eos_host_test_process_program(void) {
    eos_host_process_record *record;
    const char *value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_latest_locked();
    value = record == NULL ? "" : record->program;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
const char *eos_host_test_process_argument(uint32_t index) {
    eos_host_process_record *record;
    const char *value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_latest_locked();
    value = record != NULL && index < record->argc ? record->argv[index] : "";
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
const char *eos_host_test_process_environment(uint32_t index) {
    eos_host_process_record *record;
    const char *value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_latest_locked();
    value = record != NULL && index < record->envc ? record->envp[index] : "";
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
const char *eos_host_test_process_cwd(void) {
    eos_host_process_record *record;
    const char *value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_latest_locked();
    value = record == NULL ? "" : record->cwd;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
int32_t eos_host_test_process_inherited_fd(uint32_t index) {
    eos_host_process_record *record;
    int32_t value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_latest_locked();
    value = record != NULL && index < record->inherited_count
                ? record->inherited[index] : -1;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
uint32_t eos_host_test_process_started_identity(uint32_t identity) {
    eos_host_process_record *record;
    uint32_t value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_find_identity_locked(identity);
    value = record == NULL ? UINT32_C(0) : record->started;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
uint32_t eos_host_test_process_active_identity(uint32_t identity) {
    eos_host_process_record *record;
    uint32_t value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_find_identity_locked(identity);
    value = record == NULL ? UINT32_C(0) : record->active;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
uint32_t eos_host_test_process_argc_identity(uint32_t identity) {
    eos_host_process_record *record;
    uint32_t value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_find_identity_locked(identity);
    value = record == NULL ? UINT32_C(0) : record->argc;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
uint32_t eos_host_test_process_envc_identity(uint32_t identity) {
    eos_host_process_record *record;
    uint32_t value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_find_identity_locked(identity);
    value = record == NULL ? UINT32_C(0) : record->envc;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
const char *eos_host_test_process_argument_identity(uint32_t identity,
                                                     uint32_t index) {
    eos_host_process_record *record;
    const char *value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_find_identity_locked(identity);
    value = record != NULL && index < record->argc ? record->argv[index] : "";
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
const char *eos_host_test_process_environment_identity(uint32_t identity,
                                                        uint32_t index) {
    eos_host_process_record *record;
    const char *value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_find_identity_locked(identity);
    value = record != NULL && index < record->envc ? record->envp[index] : "";
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
const char *eos_host_test_process_cwd_identity(uint32_t identity) {
    eos_host_process_record *record;
    const char *value;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_find_identity_locked(identity);
    value = record == NULL ? "" : record->cwd;
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return value;
}
void eos_host_test_pause_closedir_after_validation(void) {
    (void)pthread_mutex_lock(&eos_host_closedir_guard);
    eos_host_closedir_pause = 1;
    eos_host_closedir_entered = 0;
    eos_host_closedir_resume = 0;
    (void)pthread_mutex_unlock(&eos_host_closedir_guard);
}
void eos_host_test_wait_closedir_validation(void) {
    (void)pthread_mutex_lock(&eos_host_closedir_guard);
    while (!eos_host_closedir_entered) {
        (void)pthread_cond_wait(&eos_host_closedir_condition,
                                &eos_host_closedir_guard);
    }
    (void)pthread_mutex_unlock(&eos_host_closedir_guard);
}
void eos_host_test_resume_closedir(void) {
    (void)pthread_mutex_lock(&eos_host_closedir_guard);
    eos_host_closedir_resume = 1;
    (void)pthread_cond_broadcast(&eos_host_closedir_condition);
    (void)pthread_mutex_unlock(&eos_host_closedir_guard);
}
static void eos_host_test_closedir_validation_point(void) {
    (void)pthread_mutex_lock(&eos_host_closedir_guard);
    if (eos_host_closedir_pause) {
        eos_host_closedir_entered = 1;
        (void)pthread_cond_broadcast(&eos_host_closedir_condition);
        while (!eos_host_closedir_resume) {
            (void)pthread_cond_wait(&eos_host_closedir_condition,
                                    &eos_host_closedir_guard);
        }
        eos_host_closedir_pause = 0;
    }
    (void)pthread_mutex_unlock(&eos_host_closedir_guard);
}
#endif

static void eos_host_tls_init(void) {
    uint32_t slot;
    for (slot = 0; slot < UINT32_C(8); ++slot) {
        if (pthread_key_create(&eos_host_tls_slot_keys[slot], NULL) != 0) abort();
    }
}

static int32_t eos_port_thread_tls_get(uint32_t slot, uintptr_t *value) {
#ifdef EOS_RUST_HOST_TEST
    atomic_store(&eos_host_last_tls_get_slot, slot);
    if (eos_host_next_tls_get_status != 0) {
        int32_t status = eos_host_next_tls_get_status;
        eos_host_next_tls_get_status = 0;
        return status;
    }
#endif
    if (slot >= UINT32_C(8) || value == NULL) return 1;
    if (pthread_once(&eos_host_tls_once, eos_host_tls_init) != 0) return 25;
    *value = (uintptr_t)pthread_getspecific(eos_host_tls_slot_keys[slot]);
    return 0;
}

static int32_t eos_port_thread_tls_set(uint32_t slot, uintptr_t value) {
#ifdef EOS_RUST_HOST_TEST
    atomic_store(&eos_host_last_tls_set_slot, slot);
    if (eos_host_next_tls_set_status != 0) {
        int32_t status = eos_host_next_tls_set_status;
        eos_host_next_tls_set_status = 0;
        return status;
    }
    if (eos_host_delayed_tls_set_status != 0) {
        if (eos_host_tls_set_successes_before_failure == 0) {
            int32_t status = eos_host_delayed_tls_set_status;
            eos_host_delayed_tls_set_status = 0;
            eos_host_tls_set_successes_before_failure = UINT32_MAX;
            return status;
        }
        --eos_host_tls_set_successes_before_failure;
    }
#endif
    if (slot >= UINT32_C(8)) return 1;
    if (pthread_once(&eos_host_tls_once, eos_host_tls_init) != 0) return 25;
    return pthread_setspecific(eos_host_tls_slot_keys[slot], (void *)value) == 0
               ? 0 : 25;
}

static uint32_t eos_port_cpu_count(void) {
    return UINT32_C(2);
}

typedef struct eos_host_thread_start {
    eos_port_thread_start start;
    void *argument;
} eos_host_thread_start;

static void *eos_host_thread_entry(void *opaque) {
    eos_host_thread_start start = *(eos_host_thread_start *)opaque;
    free(opaque);
    start.start(start.argument);
#ifdef EOS_RUST_HOST_TEST
    (void)atomic_fetch_add(&eos_host_thread_completion_count, UINT32_C(1));
#endif
    return NULL;
}

static int32_t eos_port_thread_create(const char *name,
                                      eos_port_thread_start start,
                                      void *argument,
                                      uint32_t stack_size) {
    pthread_t thread;
    pthread_attr_t attribute;
    eos_host_thread_start *context;
    int rc;
#ifdef EOS_RUST_HOST_TEST
    uint32_t completion_before;
#endif
    (void)name;
#ifdef EOS_RUST_HOST_TEST
    atomic_store(&eos_host_last_thread_stack, stack_size);
    atomic_store(&eos_host_last_thread_features, UINT32_C(0));
    atomic_store(&eos_host_last_thread_priority, UINT32_C(200));
    if (eos_host_next_thread_create_status != 0) {
        int32_t status = eos_host_next_thread_create_status;
        eos_host_next_thread_create_status = 0;
        return status;
    }
#endif
    context = (eos_host_thread_start *)malloc(sizeof(*context));
    if (context == NULL) return 15;
    context->start = start;
    context->argument = argument;
#ifdef EOS_RUST_HOST_TEST
    completion_before = atomic_load(&eos_host_thread_completion_count);
#endif
    if (pthread_attr_init(&attribute) != 0) {
        free(context);
        return 25;
    }
    (void)stack_size; /* Host PTHREAD_STACK_MIN may exceed the target minimum. */
    rc = pthread_create(&thread, &attribute, eos_host_thread_entry, context);
    (void)pthread_attr_destroy(&attribute);
    if (rc != 0) {
        free(context);
        return rc == ENOMEM || rc == EAGAIN ? 15 : 25;
    }
    if (pthread_detach(thread) != 0) abort();
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_finish_thread_before_create_returns ||
        eos_host_start_thread_then_fail_status != 0) {
        while (atomic_load(&eos_host_thread_completion_count) ==
               completion_before) {
            (void)sched_yield();
        }
    }
    if (eos_host_start_thread_then_fail_status != 0) {
        int32_t status = eos_host_start_thread_then_fail_status;
        eos_host_start_thread_then_fail_status = 0;
        return status;
    }
#endif
    return 0;
}

static uint32_t eos_port_process_capabilities(void) {
#ifdef EOS_RUST_HOST_TEST
    return atomic_load(&eos_host_process_test_capabilities);
#else
    return EOS_PORT_PROCESS_CAP_ENVIRONMENT | EOS_PORT_PROCESS_CAP_CWD |
           EOS_PORT_PROCESS_CAP_STDERR |
           EOS_PORT_PROCESS_CAP_DESCRIPTOR_INHERITANCE;
#endif
}

static int32_t eos_port_process_validate(
    const eos_port_process_request *request) {
    return request == NULL || request->name == NULL || request->program == NULL
               ? 1 : 0;
}

static void eos_host_process_copy(char *destination, size_t capacity,
                                  const char *source);

static int32_t eos_port_process_load(const eos_port_process_request *request) {
    eos_host_process_record *record = NULL;
    uint32_t index;
    if (request == NULL || request->program == NULL) {
        return 1;
    }
    if (strcmp(request->program, "/host/missing") == 0) return 12;
    (void)pthread_mutex_lock(&eos_host_process_guard);
    for (index = 0; index < EOS_HOST_PROCESS_CAPACITY; ++index) {
        if (eos_host_process_records[index].in_use == UINT32_C(0)) {
            record = &eos_host_process_records[index];
            break;
        }
    }
    if (record != NULL) {
        (void)memset(record, 0, sizeof(*record));
        record->in_use = UINT32_C(1);
        record->sequence = eos_host_process_next_sequence++;
        eos_host_process_copy(record->name, sizeof(record->name),
                              request->name);
    }
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return record == NULL ? 15 : 0;
}

static void eos_host_process_copy(char *destination, size_t capacity,
                                  const char *source) {
    if (capacity == 0) return;
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    (void)strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

static int32_t eos_port_process_run(const eos_port_process_request *request,
                                    int32_t *exit_code) {
    eos_host_process_record *record;
    uint32_t index;
    if (request == NULL || exit_code == NULL) {
        return 1;
    }
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_find_locked(request->name, 1);
    if (record == NULL) {
        (void)pthread_mutex_unlock(&eos_host_process_guard);
        return 1;
    }
    eos_host_process_copy(record->program, sizeof(record->program),
                          request->program);
    record->argc = request->argc < UINT32_C(16)
                       ? request->argc : UINT32_C(16);
    record->envc = request->envc < UINT32_C(16)
                       ? request->envc : UINT32_C(16);
    record->inherited_count =
        request->inherited_count < UINT32_C(64)
            ? request->inherited_count : UINT32_C(64);
    for (index = 0; index < record->argc; ++index) {
        eos_host_process_copy(record->argv[index],
                              sizeof(record->argv[index]),
                              request->argv[index]);
    }
    for (index = 0; index < record->envc; ++index) {
        eos_host_process_copy(record->envp[index],
                              sizeof(record->envp[index]),
                              request->envp[index]);
    }
    eos_host_process_copy(record->cwd, sizeof(record->cwd), request->cwd);
    for (index = 0; index < record->inherited_count; ++index) {
        record->inherited[index] = request->inherited[index].descriptor;
    }
    record->started = UINT32_C(1);
    record->active = UINT32_C(1);
    (void)pthread_cond_broadcast(&eos_host_process_condition);
    if (strcmp(request->program, "/host/block") == 0 ||
        strcmp(request->program, "/host/block-copy3") == 0) {
        while (record->release == UINT32_C(0) &&
               record->killed == UINT32_C(0)) {
            (void)pthread_cond_wait(&eos_host_process_condition,
                                    &eos_host_process_guard);
        }
    }
    record->active = UINT32_C(0);
    (void)pthread_mutex_unlock(&eos_host_process_guard);

    if (strcmp(request->program, "/host/run-error") == 0) {
        return 25;
    }
    if (strcmp(request->program, "/host/copy3") == 0 ||
        strcmp(request->program, "/host/block-copy3") == 0) {
        char bytes[3];
        int32_t count = request->io.read(request->io.context, bytes,
                                         (uint32_t)sizeof(bytes));
        if (count < 0) return 26;
        if (count != 0 && request->io.write_out(request->io.context, bytes,
                                                (uint32_t)count) != count) {
            return 27;
        }
    } else if (strcmp(request->program, "/host/stdout") == 0) {
        if (request->io.write_out(request->io.context, "out", 3) != 3) {
            return 27;
        }
    } else if (strcmp(request->program, "/host/stderr") == 0) {
        if (request->io.write_err(request->io.context, "err", 3) != 3) {
            return 27;
        }
    }
    *exit_code = strcmp(request->program, "/host/capture") == 0
                     ? INT32_C(23) : INT32_C(0);
    return 0;
}

static int32_t eos_port_process_kill(const char *name, uint32_t *matched) {
    eos_host_process_record *record;
    int32_t result = 0;
    if (name == NULL || matched == NULL) return 1;
    *matched = UINT32_C(0);
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_process_kill_status != 0) {
        int32_t status = eos_host_next_process_kill_status;
        eos_host_next_process_kill_status = 0;
        return status;
    }
#endif
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_find_locked(name, 1);
    if (record != NULL && record->active != UINT32_C(0)) {
        record->killed = UINT32_C(1);
        record->release = UINT32_C(1);
        *matched = UINT32_C(1);
        (void)pthread_cond_broadcast(&eos_host_process_condition);
#ifdef EOS_RUST_HOST_TEST
        if (eos_host_process_kill_after_match_status != 0) {
            result = eos_host_process_kill_after_match_status;
            eos_host_process_kill_after_match_status = 0;
        }
#endif
    }
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    return result;
}

static int32_t eos_port_process_unload(const char *name) {
    eos_host_process_record *record;
    if (name == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_process_unload_status != 0) eos_rust_abort();
#endif
    (void)pthread_mutex_lock(&eos_host_process_guard);
    record = eos_host_process_find_locked(name, 1);
    if (record != NULL) record->in_use = UINT32_C(0);
    (void)pthread_cond_broadcast(&eos_host_process_condition);
    (void)pthread_mutex_unlock(&eos_host_process_guard);
    if (record == NULL) return 1;
    return 0;
}

static int32_t eos_port_memory_alloc(uint32_t byte_count, void **memory) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_alloc_status != 0) {
        int32_t status = eos_host_next_alloc_status;
        eos_host_next_alloc_status = 0;
        *memory = NULL;
        return status;
    }
    if (eos_host_delayed_alloc_status != 0) {
        if (eos_host_alloc_successes_before_failure == 0) {
            int32_t status = eos_host_delayed_alloc_status;
            eos_host_delayed_alloc_status = 0;
            eos_host_alloc_successes_before_failure = UINT32_MAX;
            *memory = NULL;
            return status;
        }
        --eos_host_alloc_successes_before_failure;
    }
#endif
    void *allocated = malloc((size_t)byte_count);
    if (allocated == NULL) {
        *memory = NULL;
        return 15;
    }
    *memory = allocated;
    return 0;
}

static int32_t eos_port_memory_alloc_aligned(uint32_t byte_count,
                                             uint32_t alignment,
                                             void **memory) {
    void *allocated;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_aligned_status != 0) {
        int32_t status = eos_host_next_aligned_status;
        eos_host_next_aligned_status = 0;
        *memory = NULL;
        return status;
    }
#endif
    if (alignment <= (uint32_t)_Alignof(max_align_t)) {
        allocated = malloc((size_t)byte_count);
    } else {
        const size_t rounded =
            ((size_t)byte_count + (size_t)alignment - 1U) &
            ~((size_t)alignment - 1U);
        allocated = aligned_alloc((size_t)alignment, rounded);
    }
    if (allocated == NULL) {
        *memory = NULL;
        return 15;
    }
    *memory = allocated;
    return 0;
}

static int32_t eos_port_memory_realloc(uint32_t byte_count, void **memory) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_realloc_status != 0) {
        int32_t status = eos_host_next_realloc_status;
        eos_host_next_realloc_status = 0;
        return status;
    }
#endif
    void *resized = realloc(*memory, (size_t)byte_count);
    if (resized == NULL) {
        return 15;
    }
    *memory = resized;
    return 0;
}

static int32_t eos_port_memory_free(void *memory) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_free_status != 0) {
        int32_t status = eos_host_next_free_status;
        eos_host_next_free_status = 0;
        return status;
    }
#endif
    free(memory);
    return 0;
}

/* The host backend starts with an empty controlled environment. */
static int32_t eos_port_environment_get(const char *name,
                                        char *value,
                                        uint32_t value_capacity) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_native_environment_present &&
        strcmp(name, eos_host_native_environment_name) == 0) {
        size_t length = strlen(eos_host_native_environment_value);
        if (value_capacity == UINT32_C(0)) return 2;
        if (length >= (size_t)value_capacity) {
            length = (size_t)value_capacity - 1U;
        }
        (void)memcpy(value, eos_host_native_environment_value, length);
        value[length] = '\0';
        return 0;
    }
#else
    (void)name; (void)value; (void)value_capacity;
#endif
    return 12;
}

static int32_t eos_port_environment_set(const char *name, const char *value) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_environment_set_status != 0) {
        int32_t status = eos_host_next_environment_set_status;
        eos_host_next_environment_set_status = 0;
        return status;
    }
    eos_host_test_native_environment(name, value);
#else
    (void)name;
    (void)value;
#endif
    return 0;
}

static int32_t eos_port_environment_unset(const char *name) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_environment_unset_status != 0) {
        int32_t status = eos_host_next_environment_unset_status;
        eos_host_next_environment_unset_status = 0;
        return status;
    }
    if (eos_host_native_environment_present &&
        strcmp(name, eos_host_native_environment_name) == 0) {
        eos_host_native_environment_present = 0;
    }
#else
    (void)name;
#endif
    return 0;
}

static int32_t eos_port_lock_acquire(uint32_t lock_id) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_lock_status != 0) {
        int32_t status = eos_host_next_lock_status;
        eos_host_next_lock_status = 0;
        return status;
    }
    if (eos_host_delayed_lock_status != 0) {
        if (eos_host_lock_successes_before_failure == UINT32_C(0)) {
            int32_t status = eos_host_delayed_lock_status;
            eos_host_delayed_lock_status = 0;
            eos_host_lock_successes_before_failure = UINT32_MAX;
            return status;
        }
        --eos_host_lock_successes_before_failure;
    }
#endif
    if (lock_id >= EOS_PORT_LOCK_COUNT) return 1;
    return pthread_mutex_lock(&eos_host_locks[lock_id]) == 0 ? 0 : 17;
}

static int32_t eos_port_lock_release(uint32_t lock_id) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_unlock_status != 0) {
        int32_t status = eos_host_next_unlock_status;
        eos_host_next_unlock_status = 0;
        return status;
    }
#endif
    if (lock_id >= EOS_PORT_LOCK_COUNT) return 1;
    return pthread_mutex_unlock(&eos_host_locks[lock_id]) == 0 ? 0 : 20;
}

static int32_t eos_port_console_establish(uint32_t stream,
                                          uintptr_t *native_console) {
    int32_t status;
    if (stream >= UINT32_C(3) || native_console == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    status = eos_host_console_failure[stream];
    if (status != 0) {
        eos_host_console_failure[stream] = 0;
        return status;
    }
#else
    status = 0;
#endif
    *native_console = (uintptr_t)stream;
#ifdef EOS_RUST_HOST_TEST
    (void)atomic_fetch_add_explicit(&eos_host_console_open_count[stream],
                                    UINT32_C(1), memory_order_relaxed);
#endif
    return status;
}

static void eos_port_console_release(uintptr_t native_console) {
    (void)native_console;
}

static void eos_port_direct_diagnostic(const char *message) {
    size_t length = strlen(message);
    while (length != 0U) {
        ssize_t written = write(STDERR_FILENO, message, length);
        if (written <= 0) return;
        message += (size_t)written;
        length -= (size_t)written;
    }
}

typedef struct eos_host_sync {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
} eos_host_sync;

static int32_t eos_port_sync_create(eos_port_sync *sync) {
    eos_host_sync *created;
    if (sync == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_sync_create_status != 0) {
        int32_t status = eos_host_next_sync_create_status;
        eos_host_next_sync_create_status = 0;
        *sync = (eos_port_sync)0;
        return status;
    }
#endif
    created = (eos_host_sync *)malloc(sizeof(*created));
    if (created == NULL) return 15;
    if (pthread_mutex_init(&created->mutex, NULL) != 0) {
        free(created);
        return 17;
    }
    if (pthread_cond_init(&created->condition, NULL) != 0) {
        (void)pthread_mutex_destroy(&created->mutex);
        free(created);
        return 17;
    }
    *sync = (eos_port_sync)(uintptr_t)created;
#ifdef EOS_RUST_HOST_TEST
    (void)atomic_fetch_add(&eos_host_sync_create_count, UINT32_C(1));
#endif
    return 0;
}

static int32_t eos_port_sync_lock(eos_port_sync sync) {
    eos_host_sync *value = (eos_host_sync *)(uintptr_t)sync;
    return value != NULL && pthread_mutex_lock(&value->mutex) == 0 ? 0 : 17;
}

static int32_t eos_port_sync_unlock(eos_port_sync sync) {
    eos_host_sync *value = (eos_host_sync *)(uintptr_t)sync;
    return value != NULL && pthread_mutex_unlock(&value->mutex) == 0 ? 0 : 20;
}

static int32_t eos_port_sync_wait(eos_port_sync sync, uint32_t events) {
    eos_host_sync *value = (eos_host_sync *)(uintptr_t)sync;
    (void)events;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_sync_wait_status != 0) {
        int32_t status = eos_host_next_sync_wait_status;
        eos_host_next_sync_wait_status = 0;
        return status;
    }
#endif
    return value != NULL &&
                   pthread_cond_wait(&value->condition, &value->mutex) == 0
               ? 0
               : 17;
}

static int32_t eos_port_sync_broadcast(eos_port_sync sync, uint32_t events) {
    eos_host_sync *value = (eos_host_sync *)(uintptr_t)sync;
    (void)events;
    return value != NULL && pthread_cond_broadcast(&value->condition) == 0
               ? 0
               : 17;
}

static int32_t eos_port_sync_destroy(eos_port_sync sync) {
    eos_host_sync *value = (eos_host_sync *)(uintptr_t)sync;
    if (value == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_sync_destroy_status != 0) {
        int32_t status = eos_host_next_sync_destroy_status;
        eos_host_next_sync_destroy_status = 0;
        return status;
    }
#endif
    if (pthread_cond_destroy(&value->condition) != 0) return 17;
    if (pthread_mutex_destroy(&value->mutex) != 0) return 17;
    free(value);
#ifdef EOS_RUST_HOST_TEST
    (void)atomic_fetch_add(&eos_host_sync_destroy_count, UINT32_C(1));
#endif
    return 0;
}

typedef struct eos_host_public_mutex {
    pthread_mutex_t native;
} eos_host_public_mutex;

typedef struct eos_host_public_semaphore {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    uint32_t count;
    uint32_t maximum;
} eos_host_public_semaphore;

static void eos_host_realtime_deadline(uint32_t timeout_ticks,
                                       struct timespec *deadline) {
    uint64_t nanoseconds;
    (void)clock_gettime(CLOCK_REALTIME, deadline);
    nanoseconds = (uint64_t)deadline->tv_nsec +
                  ((uint64_t)timeout_ticks * UINT64_C(1000000000)) /
                      (uint64_t)eos_port_tick_rate_hz();
    deadline->tv_sec += (time_t)(nanoseconds / UINT64_C(1000000000));
    deadline->tv_nsec = (long)(nanoseconds % UINT64_C(1000000000));
}

static int32_t eos_port_mutex_create(uint32_t recursive,
                                     eos_port_mutex *mutex) {
    eos_host_public_mutex *created;
    pthread_mutexattr_t attribute;
    int result;
    if (mutex == NULL || recursive > UINT32_C(1)) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_public_mutex_create_status != 0) {
        int32_t status = eos_host_next_public_mutex_create_status;
        eos_host_next_public_mutex_create_status = 0;
        return status;
    }
#endif
    created = (eos_host_public_mutex *)malloc(sizeof(*created));
    if (created == NULL) return 15;
    if (pthread_mutexattr_init(&attribute) != 0) {
        free(created);
        return 17;
    }
    result = pthread_mutexattr_settype(
        &attribute, recursive != 0 ? PTHREAD_MUTEX_RECURSIVE
                                   : PTHREAD_MUTEX_NORMAL);
    if (result == 0) result = pthread_mutex_init(&created->native, &attribute);
    (void)pthread_mutexattr_destroy(&attribute);
    if (result != 0) {
        free(created);
        return 17;
    }
    *mutex = (eos_port_mutex)(uintptr_t)created;
    return 0;
}

static int32_t eos_port_mutex_lock(eos_port_mutex mutex,
                                   uint32_t timeout_ticks) {
    eos_host_public_mutex *value =
        (eos_host_public_mutex *)(uintptr_t)mutex;
    int result;
    if (value == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_public_mutex_lock_status != 0) {
        int32_t status = eos_host_next_public_mutex_lock_status;
        eos_host_next_public_mutex_lock_status = 0;
        return status;
    }
#endif
    if (timeout_ticks == EOS_PORT_WAIT_FOREVER) {
        result = pthread_mutex_lock(&value->native);
    } else if (timeout_ticks == EOS_PORT_NO_WAIT) {
        result = pthread_mutex_trylock(&value->native);
    } else {
        struct timespec deadline;
        eos_host_realtime_deadline(timeout_ticks, &deadline);
        result = pthread_mutex_timedlock(&value->native, &deadline);
    }
    if (result == 0) return 0;
    if (result == EBUSY) return 22;
    if (result == ETIMEDOUT) return 19;
    return 17;
}

static int32_t eos_port_mutex_unlock(eos_port_mutex mutex) {
    eos_host_public_mutex *value =
        (eos_host_public_mutex *)(uintptr_t)mutex;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_public_mutex_unlock_status != 0) {
        int32_t status = eos_host_next_public_mutex_unlock_status;
        eos_host_next_public_mutex_unlock_status = 0;
        return status;
    }
#endif
    return value != NULL && pthread_mutex_unlock(&value->native) == 0 ? 0 : 20;
}

static int32_t eos_port_mutex_destroy(eos_port_mutex mutex) {
    eos_host_public_mutex *value =
        (eos_host_public_mutex *)(uintptr_t)mutex;
    if (value == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_public_mutex_delete_status != 0) {
        int32_t status = eos_host_next_public_mutex_delete_status;
        eos_host_next_public_mutex_delete_status = 0;
        return status;
    }
#endif
    if (pthread_mutex_destroy(&value->native) != 0) return 17;
    free(value);
    return 0;
}

static int32_t eos_port_semaphore_create(uint32_t maximum_count,
                                         uint32_t initial_count,
                                         eos_port_semaphore *semaphore) {
    eos_host_public_semaphore *created;
    if (semaphore == NULL || maximum_count == 0 ||
        initial_count > maximum_count) {
        return 1;
    }
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_public_sem_create_status != 0) {
        int32_t status = eos_host_next_public_sem_create_status;
        eos_host_next_public_sem_create_status = 0;
        return status;
    }
#endif
    created = (eos_host_public_semaphore *)malloc(sizeof(*created));
    if (created == NULL) return 15;
    if (pthread_mutex_init(&created->mutex, NULL) != 0) {
        free(created);
        return 17;
    }
    if (pthread_cond_init(&created->condition, NULL) != 0) {
        (void)pthread_mutex_destroy(&created->mutex);
        free(created);
        return 17;
    }
    created->count = initial_count;
    created->maximum = maximum_count;
    *semaphore = (eos_port_semaphore)(uintptr_t)created;
    return 0;
}

static int32_t eos_port_semaphore_take(eos_port_semaphore semaphore,
                                       uint32_t timeout_ticks) {
    eos_host_public_semaphore *value =
        (eos_host_public_semaphore *)(uintptr_t)semaphore;
    struct timespec deadline;
    int result = 0;
    if (value == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_public_sem_spurious != 0) {
        eos_host_next_public_sem_spurious = 0;
        return 0;
    }
    if (eos_host_next_public_sem_take_status != 0) {
        int32_t status = eos_host_next_public_sem_take_status;
        eos_host_next_public_sem_take_status = 0;
        if (atomic_load(&eos_host_public_sem_take_failure_pause) != 0) {
            atomic_store(&eos_host_public_sem_take_failure_entered,
                         UINT32_C(1));
            while (atomic_load(&eos_host_public_sem_take_failure_release) ==
                   0) {}
        }
        return status;
    }
#endif
    if (timeout_ticks != EOS_PORT_NO_WAIT &&
        timeout_ticks != EOS_PORT_WAIT_FOREVER) {
        eos_host_realtime_deadline(timeout_ticks, &deadline);
    }
    if (pthread_mutex_lock(&value->mutex) != 0) return 17;
    while (value->count == 0 && result == 0) {
        if (timeout_ticks == EOS_PORT_NO_WAIT) {
            result = ETIMEDOUT;
        } else if (timeout_ticks == EOS_PORT_WAIT_FOREVER) {
            result = pthread_cond_wait(&value->condition, &value->mutex);
        } else {
            result = pthread_cond_timedwait(&value->condition, &value->mutex,
                                            &deadline);
        }
    }
    if (result == 0) --value->count;
    if (pthread_mutex_unlock(&value->mutex) != 0) return 20;
    if (result == 0) return 0;
    if (result == ETIMEDOUT) return 19;
    return 17;
}

static int32_t eos_port_semaphore_give(eos_port_semaphore semaphore) {
    eos_host_public_semaphore *value =
        (eos_host_public_semaphore *)(uintptr_t)semaphore;
    int result = 0;
    if (value == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_public_sem_give_status != 0) {
        int32_t status = eos_host_next_public_sem_give_status;
        eos_host_next_public_sem_give_status = 0;
        return status;
    }
#endif
    if (pthread_mutex_lock(&value->mutex) != 0) return 17;
    if (value->count == value->maximum) {
        result = 17;
    } else {
        ++value->count;
        if (pthread_cond_signal(&value->condition) != 0) result = 17;
    }
    if (pthread_mutex_unlock(&value->mutex) != 0) return 20;
    return result;
}

static int32_t eos_port_semaphore_destroy(eos_port_semaphore semaphore) {
    eos_host_public_semaphore *value =
        (eos_host_public_semaphore *)(uintptr_t)semaphore;
    if (value == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_public_sem_delete_status != 0) {
        int32_t status = eos_host_next_public_sem_delete_status;
        eos_host_next_public_sem_delete_status = 0;
        return status;
    }
#endif
    if (pthread_cond_destroy(&value->condition) != 0) return 17;
    if (pthread_mutex_destroy(&value->mutex) != 0) return 17;
    free(value);
    return 0;
}

static int32_t eos_port_monotonic_usec(uint64_t *usec) {
    struct timespec now;
    if (usec == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (pthread_mutex_lock(&eos_host_time_guard) != 0) return 17;
    if (eos_host_time_fake) {
        if (eos_host_time_script_index < eos_host_time_script_count) {
            eos_host_time_fake_now =
                eos_host_time_script[eos_host_time_script_index++];
        }
        *usec = eos_host_time_fake_now;
        if (pthread_mutex_unlock(&eos_host_time_guard) != 0) return 20;
        return 0;
    }
    if (pthread_mutex_unlock(&eos_host_time_guard) != 0) return 20;
#endif
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 25;
    *usec = (uint64_t)now.tv_sec * UINT64_C(1000000) +
            (uint64_t)now.tv_nsec / UINT64_C(1000);
    return 0;
}

static int32_t eos_port_realtime_usec(uint64_t *usec) {
    struct timespec now;
    if (usec == NULL) return 1;
#ifdef EOS_RUST_HOST_TEST
    if (pthread_mutex_lock(&eos_host_time_guard) != 0) return 17;
    if (eos_host_time_fake) {
        int32_t status = eos_host_time_realtime_status;
        *usec = eos_host_time_realtime;
        if (pthread_mutex_unlock(&eos_host_time_guard) != 0) return 20;
        return status;
    }
    if (pthread_mutex_unlock(&eos_host_time_guard) != 0) return 20;
#endif
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) return 25;
    *usec = (uint64_t)now.tv_sec * UINT64_C(1000000) +
            (uint64_t)now.tv_nsec / UINT64_C(1000);
    return 0;
}

static uint32_t eos_port_tick_rate_hz(void) { return UINT32_C(1000); }

static int32_t eos_host_time_record_delay(uint32_t value, int microseconds) {
#ifdef EOS_RUST_HOST_TEST
    int32_t status = 0;
    if (pthread_mutex_lock(&eos_host_time_guard) != 0) return 17;
    if (eos_host_time_delayed_failure != 0) {
        if (eos_host_time_delay_successes_before_failure == 0) {
            status = eos_host_time_delayed_failure;
            eos_host_time_delayed_failure = 0;
        } else {
            --eos_host_time_delay_successes_before_failure;
        }
    }
    if (status == 0 && eos_host_time_fake) {
        if (microseconds) {
            if (eos_host_time_delay_usec_count < EOS_HOST_TIME_DELAY_CAPACITY) {
                eos_host_time_delay_usec_values[eos_host_time_delay_usec_count++] =
                    value;
            }
            if (eos_host_time_delay_advances) eos_host_time_fake_now += value;
        } else {
            if (eos_host_time_delay_count < EOS_HOST_TIME_DELAY_CAPACITY) {
                eos_host_time_delay_values[eos_host_time_delay_count++] = value;
            }
            if (eos_host_time_delay_advances) {
                eos_host_time_fake_now += (uint64_t)value * UINT64_C(1000);
            }
        }
    }
    if (pthread_mutex_unlock(&eos_host_time_guard) != 0) return 20;
    if (status != 0 || eos_host_time_fake) return status;
#else
    (void)microseconds;
#endif
    {
        struct timespec request;
        uint64_t usec = microseconds ? (uint64_t)value
                                     : (uint64_t)value * UINT64_C(1000);
        request.tv_sec = (time_t)(usec / UINT64_C(1000000));
        request.tv_nsec = (long)((usec % UINT64_C(1000000)) * UINT64_C(1000));
        return nanosleep(&request, NULL) == 0 ? 0 : 25;
    }
}

static int32_t eos_port_delay_ticks(uint32_t ticks) {
    return eos_host_time_record_delay(ticks, 0);
}

static int32_t eos_port_delay_usec(uint32_t usec) {
    return eos_host_time_record_delay(usec, 1);
}

#ifdef EOS_RUST_HOST_TEST
static void eos_port_time_test_reset(void) {
    (void)pthread_mutex_lock(&eos_host_time_guard);
    eos_host_time_script_count = 0;
    eos_host_time_script_index = 0;
    eos_host_time_fake_now = 0;
    eos_host_time_fake = 1;
    eos_host_time_delay_advances = 0;
    eos_host_time_realtime = 0;
    eos_host_time_realtime_status = 0;
    eos_host_time_delay_count = 0;
    eos_host_time_delay_usec_count = 0;
    eos_host_time_delay_successes_before_failure = UINT32_MAX;
    eos_host_time_delayed_failure = 0;
    (void)pthread_mutex_unlock(&eos_host_time_guard);
}

void eos_time_test_set_monotonic_sequence(const uint64_t *values,
                                          uint32_t count) {
    (void)pthread_mutex_lock(&eos_host_time_guard);
    if (count > EOS_HOST_TIME_SCRIPT_CAPACITY) count = EOS_HOST_TIME_SCRIPT_CAPACITY;
    if (count != 0 && values != NULL) {
        (void)memcpy(eos_host_time_script, values,
                     (size_t)count * sizeof(values[0]));
        eos_host_time_fake_now = values[0];
    }
    eos_host_time_script_count = count;
    eos_host_time_script_index = 0;
    eos_host_time_fake = 1;
    (void)pthread_mutex_unlock(&eos_host_time_guard);
}

void eos_time_test_set_realtime(uint64_t value, int32_t status) {
    (void)pthread_mutex_lock(&eos_host_time_guard);
    eos_host_time_realtime = value;
    eos_host_time_realtime_status = status;
    eos_host_time_fake = 1;
    (void)pthread_mutex_unlock(&eos_host_time_guard);
}

void eos_time_test_fail_delay_after(uint32_t successful_calls,
                                    int32_t status) {
    (void)pthread_mutex_lock(&eos_host_time_guard);
    eos_host_time_delay_successes_before_failure = successful_calls;
    eos_host_time_delayed_failure = status;
    (void)pthread_mutex_unlock(&eos_host_time_guard);
}

void eos_time_test_set_delay_advances_clock(int enabled) {
    (void)pthread_mutex_lock(&eos_host_time_guard);
    eos_host_time_delay_advances = enabled;
    (void)pthread_mutex_unlock(&eos_host_time_guard);
}

uint32_t eos_time_test_delay_count(void) { return eos_host_time_delay_count; }
uint32_t eos_time_test_delay_value(uint32_t index) {
    return index < eos_host_time_delay_count ? eos_host_time_delay_values[index]
                                             : UINT32_MAX;
}
uint32_t eos_time_test_delay_usec_count(void) {
    return eos_host_time_delay_usec_count;
}
uint32_t eos_time_test_delay_usec_value(uint32_t index) {
    return index < eos_host_time_delay_usec_count
               ? eos_host_time_delay_usec_values[index]
               : UINT32_MAX;
}
#endif

static int32_t eos_host_status_from_errno(int error_number) {
    switch (error_number) {
    case ENOENT: return 12;
    case EEXIST: return 13;
    case EACCES: return 16;
    case EBUSY: return 17;
    case EROFS: return 18;
    case ENOMEM: return 15;
    default: return 25;
    }
}

static int32_t eos_host_compat_errno(int error_number) {
    switch (error_number) {
    case ENOENT: return EOS_ERRNO_NO_ENTRY;
    case EEXIST: return EOS_ERRNO_EXISTS;
    case EACCES:
    case EPERM: return EOS_ERRNO_ACCESS;
    case EBUSY: return EOS_ERRNO_BUSY;
    case EROFS: return EOS_ERRNO_READ_ONLY_FS;
    case ENOMEM: return EOS_ERRNO_NO_MEMORY;
    case ENOTEMPTY: return EOS_ERRNO_NOT_EMPTY;
    case ENOTDIR: return EOS_ERRNO_NOT_DIRECTORY;
    case EISDIR: return EOS_ERRNO_IS_DIRECTORY;
    case EINVAL: return EOS_ERRNO_INVALID;
    default: return EOS_ERRNO_IO;
    }
}

static eos_port_result eos_host_path_result(int result) {
    eos_port_result translated;
    if (result == 0) {
        translated.status = 0;
        translated.error_number = 0;
    } else {
        translated.status = eos_host_status_from_errno(errno);
        translated.error_number = eos_host_compat_errno(errno);
    }
    return translated;
}

static int eos_host_file_descriptor(eos_port_file file) {
    return (int)file.words[0] - 1;
}

static eos_port_result eos_port_file_open(const char *path, uint32_t flags,
                                          eos_port_file *file) {
    int native_flags = 0;
    int descriptor;
    eos_port_result result = {0, 0};
    switch (flags & EOS_RUST_O_ACCMODE) {
    case EOS_RUST_O_RDONLY: native_flags |= O_RDONLY; break;
    case EOS_RUST_O_WRONLY: native_flags |= O_WRONLY; break;
    case EOS_RUST_O_RDWR: native_flags |= O_RDWR; break;
    default:
        result.status = 1;
        return result;
    }
    if ((flags & EOS_RUST_O_CREAT) != 0) native_flags |= O_CREAT;
    if ((flags & EOS_RUST_O_EXCL) != 0) native_flags |= O_EXCL;
    if ((flags & EOS_RUST_O_TRUNC) != 0) native_flags |= O_TRUNC;
    descriptor = open(path, native_flags, (mode_t)0666);
    if (descriptor < 0) {
        result.status = eos_host_status_from_errno(errno);
        return result;
    }
    file->words[0] = (uintptr_t)(descriptor + 1);
    file->words[1] = 0;
    return result;
}

static int32_t eos_port_file_read(eos_port_file file, void *buffer,
                                  uint32_t byte_count, uint32_t *completed) {
    uint32_t requested = byte_count;
    int32_t trailing = 0;
    ssize_t result;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_file_partial_read_bytes != UINT32_MAX) {
        if (requested > eos_host_file_partial_read_bytes) {
            requested = eos_host_file_partial_read_bytes;
        }
        trailing = eos_host_file_partial_read_status;
        eos_host_file_partial_read_bytes = UINT32_MAX;
        eos_host_file_partial_read_status = 0;
    }
#endif
    result = read(eos_host_file_descriptor(file), buffer, (size_t)requested);
    if (result < 0) {
        *completed = 0;
        return eos_host_status_from_errno(errno);
    }
    *completed = (uint32_t)result;
    return trailing;
}

static int32_t eos_port_file_write(eos_port_file file, const void *buffer,
                                   uint32_t byte_count, uint32_t *completed) {
    uint32_t requested = byte_count;
    int32_t trailing = 0;
    ssize_t result;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_file_partial_write_bytes != UINT32_MAX) {
        if (requested > eos_host_file_partial_write_bytes) {
            requested = eos_host_file_partial_write_bytes;
        }
        trailing = eos_host_file_partial_write_status;
        eos_host_file_partial_write_bytes = UINT32_MAX;
        eos_host_file_partial_write_status = 0;
    }
#endif
    result = write(eos_host_file_descriptor(file), buffer, (size_t)requested);
    if (result < 0) {
        *completed = 0;
        return eos_host_status_from_errno(errno);
    }
    *completed = (uint32_t)result;
    return trailing;
}

static int32_t eos_port_file_seek(eos_port_file file, int64_t offset,
                                  int32_t origin) {
    int native_origin;
    switch (origin) {
    case EOS_RUST_SEEK_SET: native_origin = SEEK_SET; break;
    case EOS_RUST_SEEK_CUR: native_origin = SEEK_CUR; break;
    case EOS_RUST_SEEK_END: native_origin = SEEK_END; break;
    default: return 1;
    }
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_delayed_seek_status != 0) {
        if (eos_host_seek_successes_before_failure == UINT32_C(0)) {
            int32_t status = eos_host_delayed_seek_status;
            eos_host_delayed_seek_status = 0;
            eos_host_seek_successes_before_failure = UINT32_MAX;
            return status;
        }
        --eos_host_seek_successes_before_failure;
    }
#endif
    return lseek(eos_host_file_descriptor(file), (off_t)offset, native_origin) < 0
               ? eos_host_status_from_errno(errno)
               : 0;
}

static int32_t eos_port_file_tell(eos_port_file file, int64_t *offset) {
    off_t result = lseek(eos_host_file_descriptor(file), (off_t)0, SEEK_CUR);
    if (result < 0) return eos_host_status_from_errno(errno);
    *offset = (int64_t)result;
    return 0;
}

static int32_t eos_port_file_flush(eos_port_file file) {
    return fsync(eos_host_file_descriptor(file)) == 0
               ? 0
               : eos_host_status_from_errno(errno);
}

static void eos_host_fill_stat(const struct stat *native, eos_port_stat *out) {
    out->entry_id = (uint64_t)native->st_ino;
    out->byte_count = native->st_size < 0 ? 0 : (uint64_t)native->st_size;
    out->utc_seconds = (int64_t)native->st_mtime;
    out->type = S_ISDIR(native->st_mode) ? EOS_PORT_FILE_TYPE_DIRECTORY
                                        : EOS_PORT_FILE_TYPE_REGULAR;
    out->read_only = (native->st_mode & S_IWUSR) == 0 ? UINT32_C(1)
                                                      : UINT32_C(0);
}

static int32_t eos_port_file_stat(eos_port_file file, eos_port_stat *metadata) {
    struct stat native;
    if (fstat(eos_host_file_descriptor(file), &native) != 0) {
        return eos_host_status_from_errno(errno);
    }
    eos_host_fill_stat(&native, metadata);
    return 0;
}

static int32_t eos_port_file_close(eos_port_file file) {
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_file_close_status != 0) {
        int32_t status = eos_host_next_file_close_status;
        eos_host_next_file_close_status = 0;
        return status;
    }
#endif
    return close(eos_host_file_descriptor(file)) == 0
               ? 0
               : eos_host_status_from_errno(errno);
}

static int32_t eos_port_path_stat(const char *path, eos_port_stat *metadata) {
    struct stat native;
    if (stat(path, &native) != 0) return eos_host_status_from_errno(errno);
    eos_host_fill_stat(&native, metadata);
    return 0;
}

static int32_t eos_port_path_mkdir(const char *path) {
    return mkdir(path, (mode_t)0777) == 0 ? 0 : eos_host_status_from_errno(errno);
}

static eos_port_result eos_port_path_unlink(const char *path) {
    return eos_host_path_result(unlink(path));
}

static eos_port_result eos_port_path_rmdir(const char *path) {
    return eos_host_path_result(rmdir(path));
}

static eos_port_result eos_port_path_rename(const char *old_path,
                                             const char *new_path) {
    return eos_host_path_result(rename(old_path, new_path));
}

static int32_t eos_port_directory_count(const char *path, uint32_t *count) {
    DIR *directory = opendir(path);
    struct dirent *entry;
    uint32_t value = 0;
    if (directory == NULL) return eos_host_status_from_errno(errno);
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            if (value == UINT32_MAX) {
                (void)closedir(directory);
                return 15;
            }
            ++value;
        }
    }
    if (closedir(directory) != 0) return 25;
    *count = value;
    return 0;
}

static int32_t eos_port_directory_list(const char *path,
                                       eos_port_dir_entry *entries,
                                       uint32_t capacity,
                                       uint32_t *count) {
    DIR *directory = opendir(path);
    struct dirent *entry;
    uint32_t used = 0;
    if (directory == NULL) return eos_host_status_from_errno(errno);
    while ((entry = readdir(directory)) != NULL) {
        size_t length;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (used >= capacity) {
            (void)closedir(directory);
            return 17;
        }
        length = strlen(entry->d_name);
        if (length > EOS_RUST_NAME_MAX) {
            (void)closedir(directory);
            return 1;
        }
        entries[used].entry_id = (uint64_t)entry->d_ino;
        {
            char child[EOS_RUST_PATH_MAX];
            struct stat native;
            size_t path_length = strlen(path);
            if (path_length + length + 2U > sizeof(child)) {
                (void)closedir(directory);
                return 1;
            }
            (void)memcpy(child, path, path_length);
            if (path_length != 1U || path[0] != '/') child[path_length++] = '/';
            (void)memcpy(child + path_length, entry->d_name, length + 1U);
            if (stat(child, &native) != 0) {
                (void)closedir(directory);
                return eos_host_status_from_errno(errno);
            }
            entries[used].type = S_ISDIR(native.st_mode)
                                     ? EOS_PORT_FILE_TYPE_DIRECTORY
                                     : EOS_PORT_FILE_TYPE_REGULAR;
        }
        entries[used].name_length = (uint32_t)length;
        (void)memcpy(entries[used].name, entry->d_name, length + 1U);
        ++used;
    }
    if (closedir(directory) != 0) return 25;
    *count = used;
    return 0;
}

static int32_t eos_port_console_read(uintptr_t stream, void *buffer,
                                     uint32_t byte_count,
                                     uint32_t *completed) {
    uint32_t available;
    uint32_t requested = byte_count;
    int32_t trailing = 0;
    if (stream != UINT32_C(0)) return 1;
    (void)pthread_mutex_lock(&eos_host_console_guard);
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_console_partial_bytes[0] != UINT32_MAX) {
        if (requested > eos_host_console_partial_bytes[0]) {
            requested = eos_host_console_partial_bytes[0];
        }
        trailing = eos_host_console_partial_status[0];
        eos_host_console_partial_bytes[0] = UINT32_MAX;
        eos_host_console_partial_status[0] = 0;
    }
#endif
    available = eos_host_console_in_length - eos_host_console_in_offset;
    if (requested > available) requested = available;
    (void)memcpy(buffer, eos_host_console_in + eos_host_console_in_offset,
                 requested);
    eos_host_console_in_offset += requested;
    (void)pthread_mutex_unlock(&eos_host_console_guard);
    *completed = requested;
    return trailing;
}

static int32_t eos_port_console_write(uintptr_t stream, const void *buffer,
                                      uint32_t byte_count,
                                      uint32_t *completed) {
    uint32_t requested = byte_count;
    uint32_t index;
    uint32_t available;
    int32_t trailing = 0;
    if (stream < UINT32_C(1) || stream > UINT32_C(2)) return 1;
    index = (uint32_t)stream - UINT32_C(1);
    (void)pthread_mutex_lock(&eos_host_console_guard);
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_console_partial_bytes[(uint32_t)stream] != UINT32_MAX) {
        if (requested > eos_host_console_partial_bytes[(uint32_t)stream]) {
            requested = eos_host_console_partial_bytes[(uint32_t)stream];
        }
        trailing = eos_host_console_partial_status[(uint32_t)stream];
        eos_host_console_partial_bytes[(uint32_t)stream] = UINT32_MAX;
        eos_host_console_partial_status[(uint32_t)stream] = 0;
    }
#endif
    available = (uint32_t)sizeof(eos_host_console_out[index]) -
                eos_host_console_out_length[index];
    if (requested > available) requested = available;
    (void)memcpy(eos_host_console_out[index] + eos_host_console_out_length[index],
                 buffer, requested);
    eos_host_console_out_length[index] += requested;
    (void)pthread_mutex_unlock(&eos_host_console_guard);
    *completed = requested;
    return trailing;
}

static int32_t eos_port_hostname(char *name, uint32_t capacity) {
    size_t length;
    (void)pthread_mutex_lock(&eos_host_console_guard);
    length = strlen(eos_host_hostname);
    if (length + 1U > capacity) {
        (void)pthread_mutex_unlock(&eos_host_console_guard);
        return 1;
    }
    (void)memcpy(name, eos_host_hostname, length + 1U);
    (void)pthread_mutex_unlock(&eos_host_console_guard);
    return 0;
}

static int32_t eos_host_network_error(int error_number) {
    if (error_number == 0) return 0;
    if (error_number == EACCES || error_number == EPERM) {
        return EOS_ERRNO_ACCESS;
    }
    if (error_number == EAGAIN || error_number == EWOULDBLOCK) {
        return EOS_ERRNO_WOULD_BLOCK;
    }
    if (error_number == EINPROGRESS) return EOS_ERRNO_IN_PROGRESS;
    if (error_number == EALREADY) return EOS_ERRNO_ALREADY;
    if (error_number == EBADF) return EOS_ERRNO_BAD_DESCRIPTOR;
    if (error_number == ENOTSOCK) return EOS_ERRNO_NOT_SOCKET;
    if (error_number == EDESTADDRREQ) return EOS_ERRNO_DESTINATION_REQUIRED;
    if (error_number == EMSGSIZE) return EOS_ERRNO_MESSAGE_SIZE;
    if (error_number == EPROTOTYPE) return EOS_ERRNO_PROTOCOL_TYPE;
    if (error_number == ENOPROTOOPT) return EOS_ERRNO_NO_PROTOCOL_OPTION;
    if (error_number == EPROTONOSUPPORT) {
        return EOS_ERRNO_PROTOCOL_NOT_SUPPORTED;
    }
    if (error_number == EAFNOSUPPORT) {
        return EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED;
    }
    if (error_number == EADDRINUSE) return EOS_ERRNO_ADDRESS_IN_USE;
    if (error_number == EADDRNOTAVAIL) return EOS_ERRNO_ADDRESS_NOT_AVAILABLE;
    if (error_number == ENETDOWN) return EOS_ERRNO_NETWORK_DOWN;
    if (error_number == ENETUNREACH) return EOS_ERRNO_NETWORK_UNREACHABLE;
    if (error_number == ENETRESET) return EOS_ERRNO_NETWORK_RESET;
    if (error_number == ECONNABORTED) return EOS_ERRNO_CONNECTION_ABORTED;
    if (error_number == ECONNRESET) return EOS_ERRNO_CONNECTION_RESET;
    if (error_number == ENOBUFS) return EOS_ERRNO_NO_BUFFERS;
    if (error_number == EISCONN) return EOS_ERRNO_IS_CONNECTED;
    if (error_number == ENOTCONN) return EOS_ERRNO_NOT_CONNECTED;
#ifdef ESHUTDOWN
    if (error_number == ESHUTDOWN) return EOS_ERRNO_SHUTDOWN;
#endif
    if (error_number == ETIMEDOUT) return EOS_ERRNO_TIMED_OUT;
    if (error_number == ECONNREFUSED) return EOS_ERRNO_CONNECTION_REFUSED;
#ifdef EHOSTDOWN
    if (error_number == EHOSTDOWN) return EOS_ERRNO_HOST_DOWN;
#endif
    if (error_number == EHOSTUNREACH) return EOS_ERRNO_HOST_UNREACHABLE;
    if (error_number == EINVAL) return EOS_ERRNO_INVALID;
    if (error_number == ENOMEM) return EOS_ERRNO_NO_MEMORY;
    if (error_number == EFAULT) return EOS_ERRNO_FAULT;
    if (error_number == EINTR) return EOS_ERRNO_INTERRUPTED;
    if (error_number == EPIPE) return EOS_ERRNO_PIPE;
    return EOS_ERRNO_IO;
}

#ifdef EOS_RUST_HOST_TEST
int32_t eos_host_test_network_error(int32_t native_error) {
    return eos_host_network_error(native_error);
}
#endif

static int eos_host_socket_fd(eos_port_socket socket) {
    return (int)(socket - (eos_port_socket)1);
}

static eos_port_socket eos_host_socket_value(int descriptor) {
    return (eos_port_socket)(uint32_t)descriptor + (eos_port_socket)1;
}

static int32_t eos_host_address_to_native(
    const eos_port_socket_address *source,
    struct sockaddr_storage *destination,
    socklen_t *length) {
    (void)memset(destination, 0, sizeof(*destination));
    if (source->family == EOS_RUST_AF_INET) {
        struct sockaddr_in *value = (struct sockaddr_in *)destination;
        value->sin_family = AF_INET;
        value->sin_port = source->port;
        (void)memcpy(&value->sin_addr, source->address, 4);
        *length = (socklen_t)sizeof(*value);
        return 0;
    }
    if (source->family == EOS_RUST_AF_INET6) {
        struct sockaddr_in6 *value = (struct sockaddr_in6 *)destination;
        value->sin6_family = AF_INET6;
        value->sin6_port = source->port;
        value->sin6_flowinfo = source->flowinfo;
        value->sin6_scope_id = source->scope_id;
        (void)memcpy(&value->sin6_addr, source->address, 16);
        *length = (socklen_t)sizeof(*value);
        return 0;
    }
    return EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED;
}

static int32_t eos_host_address_from_native(
    const struct sockaddr *source,
    socklen_t length,
    eos_port_socket_address *destination) {
    (void)memset(destination, 0, sizeof(*destination));
    if (source->sa_family == AF_INET &&
        length >= (socklen_t)sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *value = (const struct sockaddr_in *)source;
        destination->family = EOS_RUST_AF_INET;
        destination->port = value->sin_port;
        (void)memcpy(destination->address, &value->sin_addr, 4);
        return 0;
    }
    if (source->sa_family == AF_INET6 &&
        length >= (socklen_t)sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *value = (const struct sockaddr_in6 *)source;
        destination->family = EOS_RUST_AF_INET6;
        destination->port = value->sin6_port;
        destination->flowinfo = value->sin6_flowinfo;
        destination->scope_id = value->sin6_scope_id;
        (void)memcpy(destination->address, &value->sin6_addr, 16);
        return 0;
    }
    return EOS_ERRNO_ADDRESS_FAMILY_NOT_SUPPORTED;
}

static int32_t eos_host_message_flags(int32_t flags) {
    int32_t native_flags = 0;
    if ((flags & EOS_RUST_MSG_OOB) != 0) native_flags |= MSG_OOB;
    if ((flags & EOS_RUST_MSG_PEEK) != 0) native_flags |= MSG_PEEK;
    if ((flags & EOS_RUST_MSG_DONTROUTE) != 0) native_flags |= MSG_DONTROUTE;
    if ((flags & EOS_RUST_MSG_DONTWAIT) != 0) native_flags |= MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
    if ((flags & EOS_RUST_MSG_NOSIGNAL) != 0) native_flags |= MSG_NOSIGNAL;
#endif
    return native_flags;
}

static eos_port_socket_create_result eos_port_socket_create(int32_t domain,
                                                            int32_t type,
                                                            int32_t protocol) {
    eos_port_socket_create_result result = {EOS_PORT_SOCKET_INVALID, 0};
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_socket_create_error != 0) {
        result.error_number = eos_host_next_socket_create_error;
        eos_host_next_socket_create_error = 0;
        return result;
    }
#endif
    int descriptor = socket(domain == EOS_RUST_AF_INET ? AF_INET : AF_INET6,
                            type == EOS_RUST_SOCK_STREAM ? SOCK_STREAM
                                                         : SOCK_DGRAM,
                            protocol);
    if (descriptor < 0) result.error_number = eos_host_network_error(errno);
    else result.socket = eos_host_socket_value(descriptor);
    return result;
}

static int32_t eos_port_socket_close(eos_port_socket socket_value) {
    int result;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_socket_close_error != 0) {
        int32_t error = eos_host_next_socket_close_error;
        eos_host_next_socket_close_error = 0;
        return error;
    }
#endif
    result = close(eos_host_socket_fd(socket_value));
#ifdef EOS_RUST_HOST_TEST
    if (result == 0) (void)atomic_fetch_add(&eos_host_socket_close_count, 1);
#endif
    return result == 0 ? 0 : eos_host_network_error(errno);
}

static int32_t eos_port_socket_bind(eos_port_socket socket_value,
                                    const eos_port_socket_address *address) {
    struct sockaddr_storage native;
    socklen_t length;
    int32_t error = eos_host_address_to_native(address, &native, &length);
    if (error != 0) return error;
    return bind(eos_host_socket_fd(socket_value),
                (const struct sockaddr *)&native, length) == 0
               ? 0 : eos_host_network_error(errno);
}

static int32_t eos_port_socket_connect(eos_port_socket socket_value,
                                       const eos_port_socket_address *address) {
    struct sockaddr_storage native;
    socklen_t length;
    int32_t error = eos_host_address_to_native(address, &native, &length);
    if (error != 0) return error;
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_next_socket_connect_error != 0) {
        error = eos_host_next_socket_connect_error;
        eos_host_next_socket_connect_error = 0;
        return error;
    }
#endif
    return connect(eos_host_socket_fd(socket_value),
                   (const struct sockaddr *)&native, length) == 0
               ? 0 : eos_host_network_error(errno);
}

static int32_t eos_port_socket_listen(eos_port_socket socket_value,
                                      int32_t backlog) {
    return listen(eos_host_socket_fd(socket_value), backlog) == 0
               ? 0 : eos_host_network_error(errno);
}

static eos_port_socket_create_result eos_port_socket_accept(
    eos_port_socket socket_value,
    eos_port_socket_address *address) {
    eos_port_socket_create_result result = {EOS_PORT_SOCKET_INVALID, 0};
    struct sockaddr_storage native;
    socklen_t length = (socklen_t)sizeof(native);
    int descriptor = accept(eos_host_socket_fd(socket_value),
                            (struct sockaddr *)&native, &length);
    if (descriptor < 0) {
        result.error_number = eos_host_network_error(errno);
        return result;
    }
    result.error_number = eos_host_address_from_native(
        (const struct sockaddr *)&native, length, address);
    if (result.error_number != 0) {
        if (eos_port_socket_close(eos_host_socket_value(descriptor)) != 0) {
            eos_rust_abort();
        }
        return result;
    }
    result.socket = eos_host_socket_value(descriptor);
    return result;
}

static eos_port_socket_io_result eos_host_io_result(ssize_t count) {
    eos_port_socket_io_result result;
    if (count < 0) {
        result.count = -1;
        result.error_number = eos_host_network_error(errno);
    } else {
        result.count = (int32_t)count;
        result.error_number = 0;
    }
    return result;
}

static eos_port_socket_io_result eos_port_socket_send(
    eos_port_socket socket_value, const void *buffer, uint32_t byte_count,
    int32_t flags) {
    uint32_t requested = byte_count;
#ifdef EOS_RUST_HOST_TEST
    int32_t trailing = eos_host_socket_partial_send_error;
    if (eos_host_socket_partial_send_bytes != UINT32_MAX) {
        if (requested > eos_host_socket_partial_send_bytes) {
            requested = eos_host_socket_partial_send_bytes;
        }
        eos_host_socket_partial_send_bytes = UINT32_MAX;
        eos_host_socket_partial_send_error = 0;
        if (requested == 0 && trailing != 0) {
            return (eos_port_socket_io_result){-1, trailing};
        }
    }
#endif
    return eos_host_io_result(send(eos_host_socket_fd(socket_value), buffer,
                                   requested, eos_host_message_flags(flags)));
}

static eos_port_socket_io_result eos_port_socket_receive(
    eos_port_socket socket_value, void *buffer, uint32_t byte_count,
    int32_t flags) {
    uint32_t requested = byte_count;
#ifdef EOS_RUST_HOST_TEST
    int32_t trailing = eos_host_socket_partial_receive_error;
    if (eos_host_socket_partial_receive_bytes != UINT32_MAX) {
        if (requested > eos_host_socket_partial_receive_bytes) {
            requested = eos_host_socket_partial_receive_bytes;
        }
        eos_host_socket_partial_receive_bytes = UINT32_MAX;
        eos_host_socket_partial_receive_error = 0;
        if (requested == 0 && trailing != 0) {
            return (eos_port_socket_io_result){-1, trailing};
        }
    }
#endif
    return eos_host_io_result(recv(eos_host_socket_fd(socket_value), buffer,
                                   requested, eos_host_message_flags(flags)));
}

static eos_port_socket_io_result eos_port_socket_send_to(
    eos_port_socket socket_value, const void *buffer, uint32_t byte_count,
    int32_t flags, const eos_port_socket_address *destination) {
    struct sockaddr_storage native;
    socklen_t length;
    int32_t error = eos_host_address_to_native(destination, &native, &length);
    if (error != 0) return (eos_port_socket_io_result){-1, error};
    return eos_host_io_result(sendto(eos_host_socket_fd(socket_value), buffer,
                                     byte_count, eos_host_message_flags(flags),
                                     (const struct sockaddr *)&native, length));
}

static eos_port_socket_io_result eos_port_socket_receive_from(
    eos_port_socket socket_value, void *buffer, uint32_t byte_count,
    int32_t flags, eos_port_socket_address *source) {
    struct sockaddr_storage native;
    socklen_t length = (socklen_t)sizeof(native);
    eos_port_socket_io_result result = eos_host_io_result(
        recvfrom(eos_host_socket_fd(socket_value), buffer, byte_count,
                 eos_host_message_flags(flags),
                 source == NULL ? NULL : (struct sockaddr *)&native,
                 source == NULL ? NULL : &length));
    if (result.count >= 0 && source != NULL) {
        int32_t error = eos_host_address_from_native(
            (const struct sockaddr *)&native, length, source);
        if (error != 0) {
            if (result.count != 0) {
                return (eos_port_socket_io_result){-1, error};
            }
            (void)memset(source, 0, sizeof(*source));
        }
    }
    return result;
}

static int32_t eos_port_socket_shutdown(eos_port_socket socket_value,
                                        int32_t how) {
    return shutdown(eos_host_socket_fd(socket_value), how) == 0
               ? 0 : eos_host_network_error(errno);
}

static int32_t eos_host_socket_address_query(eos_port_socket socket_value,
                                             eos_port_socket_address *address,
                                             int peer) {
    struct sockaddr_storage native;
    socklen_t length = (socklen_t)sizeof(native);
    int result = peer ? getpeername(eos_host_socket_fd(socket_value),
                                    (struct sockaddr *)&native, &length)
                      : getsockname(eos_host_socket_fd(socket_value),
                                    (struct sockaddr *)&native, &length);
    if (result != 0) return eos_host_network_error(errno);
    return eos_host_address_from_native((const struct sockaddr *)&native,
                                        length, address);
}

static int32_t eos_port_socket_local_address(
    eos_port_socket socket_value, eos_port_socket_address *address) {
    return eos_host_socket_address_query(socket_value, address, 0);
}

static int32_t eos_port_socket_remote_address(
    eos_port_socket socket_value, eos_port_socket_address *address) {
    return eos_host_socket_address_query(socket_value, address, 1);
}

static int32_t eos_port_socket_set_timeout(eos_port_socket socket_value,
                                           uint32_t receive,
                                           uint32_t timeout_ticks) {
    struct timeval value;
    uint32_t rate = eos_port_tick_rate_hz();
#ifdef EOS_RUST_HOST_TEST
    if (eos_host_timeout_log_count < EOS_HOST_TIMEOUT_LOG_CAPACITY) {
        eos_host_timeout_log_receive[eos_host_timeout_log_count] = receive;
        eos_host_timeout_log_ticks[eos_host_timeout_log_count] = timeout_ticks;
        ++eos_host_timeout_log_count;
    }
    if (eos_host_delayed_timeout_error != 0) {
        if (eos_host_timeout_successes_before_failure == 0) {
            int32_t error = eos_host_delayed_timeout_error;
            eos_host_delayed_timeout_error = 0;
            eos_host_timeout_successes_before_failure = UINT32_MAX;
            return error;
        }
        --eos_host_timeout_successes_before_failure;
    }
#endif
    if (timeout_ticks == EOS_PORT_WAIT_FOREVER) {
        value.tv_sec = 0;
        value.tv_usec = 0;
    } else if (timeout_ticks == EOS_PORT_NO_WAIT) {
        value.tv_sec = 0;
        value.tv_usec = 1;
    } else {
        uint64_t usec = ((uint64_t)timeout_ticks * UINT64_C(1000000) +
                         rate - 1U) / rate;
        value.tv_sec = (time_t)(usec / UINT64_C(1000000));
        value.tv_usec = (suseconds_t)(usec % UINT64_C(1000000));
    }
    return setsockopt(eos_host_socket_fd(socket_value), SOL_SOCKET,
                      receive ? SO_RCVTIMEO : SO_SNDTIMEO,
                      &value, (socklen_t)sizeof(value)) == 0
               ? 0 : eos_host_network_error(errno);
}

static int32_t eos_port_socket_set_nonblocking(eos_port_socket socket_value,
                                               uint32_t enabled) {
    int descriptor = eos_host_socket_fd(socket_value);
    int flags = fcntl(descriptor, F_GETFL, 0);
    if (flags < 0) return eos_host_network_error(errno);
    if (enabled != 0) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(descriptor, F_SETFL, flags) == 0
               ? 0 : eos_host_network_error(errno);
}

static int32_t eos_port_socket_set_integer_option(eos_port_socket socket_value,
                                                  int32_t option_name,
                                                  int32_t value) {
    int native_name = option_name == EOS_RUST_SO_SNDBUF ? SO_SNDBUF : SO_RCVBUF;
    return setsockopt(eos_host_socket_fd(socket_value), SOL_SOCKET,
                      native_name, &value, (socklen_t)sizeof(value)) == 0
               ? 0 : eos_host_network_error(errno);
}

static int32_t eos_port_socket_connection_error(eos_port_socket socket_value,
                                                int32_t *error_number) {
    int native_error = 0;
    socklen_t length = (socklen_t)sizeof(native_error);
    if (getsockopt(eos_host_socket_fd(socket_value), SOL_SOCKET, SO_ERROR,
                   &native_error, &length) != 0) {
        return eos_host_network_error(errno);
    }
    *error_number = eos_host_network_error(native_error);
    return 0;
}

static int32_t eos_port_socket_poll(const eos_port_socket *sockets,
                                    const uint32_t *requested,
                                    uint32_t *observed,
                                    uint32_t socket_count,
                                    uint32_t timeout_ticks) {
    struct pollfd native[EOS_PORT_SOCKET_POLL_CAPACITY];
    uint32_t index;
    int timeout;
    int result;
    uint32_t rate = eos_port_tick_rate_hz();
    if (timeout_ticks == EOS_PORT_WAIT_FOREVER) timeout = -1;
    else {
        uint64_t milliseconds =
            ((uint64_t)timeout_ticks * UINT64_C(1000) + rate - 1U) / rate;
        timeout = milliseconds > (uint64_t)INT_MAX ? INT_MAX
                                                   : (int)milliseconds;
    }
    for (index = 0; index < socket_count; ++index) {
        native[index].fd = eos_host_socket_fd(sockets[index]);
        native[index].events = 0;
        native[index].revents = 0;
        if ((requested[index] & EOS_PORT_SOCKET_EVENT_READ) != 0) {
            native[index].events |= POLLIN;
        }
        if ((requested[index] & EOS_PORT_SOCKET_EVENT_WRITE) != 0) {
            native[index].events |= POLLOUT;
        }
        if ((requested[index] & EOS_PORT_SOCKET_EVENT_PRIORITY) != 0) {
            native[index].events |= POLLPRI;
        }
    }
    result = poll(native, socket_count, timeout);
    if (result < 0) return eos_host_network_error(errno);
#ifdef EOS_RUST_HOST_TEST
    if (atomic_load(&eos_host_poll_pause) != 0) {
        atomic_store(&eos_host_poll_entered, 1);
        while (atomic_load(&eos_host_poll_release) == 0) (void)sched_yield();
    }
#endif
    for (index = 0; index < socket_count; ++index) {
        observed[index] = 0;
        if ((native[index].revents & POLLIN) != 0) {
            observed[index] |= EOS_PORT_SOCKET_EVENT_READ;
        }
        if ((native[index].revents & POLLPRI) != 0) {
            observed[index] |= EOS_PORT_SOCKET_EVENT_PRIORITY;
        }
        if ((native[index].revents & POLLOUT) != 0) {
            observed[index] |= EOS_PORT_SOCKET_EVENT_WRITE;
        }
        if ((native[index].revents & (POLLERR | POLLNVAL)) != 0) {
            observed[index] |= EOS_PORT_SOCKET_EVENT_ERROR;
        }
        if ((native[index].revents & (POLLHUP
#ifdef POLLRDHUP
                                       | POLLRDHUP
#endif
                                      )) != 0) {
            observed[index] |= EOS_PORT_SOCKET_EVENT_HANGUP;
        }
#ifdef EOS_RUST_HOST_TEST
        observed[index] |= atomic_load(&eos_host_poll_forced_events);
#endif
    }
    return 0;
}

static uint64_t eos_port_hash_bytes(const char *text) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    while (*text != '\0') {
        hash ^= (uint8_t)*text;
        hash *= UINT64_C(0x100000001b3);
        ++text;
    }
    return hash;
}

static void eos_port_hash_seed_sources(eos_hash_seed_sources *sources) {
    struct timespec now = {0, 0};
    unsigned char *heap_marker = malloc(1U);
    uint64_t stack_marker = 0;
    static const uint64_t code_marker = UINT64_C(0x454f532d484f5354);

    (void)timespec_get(&now, TIME_UTC);
    sources->timer_usec =
        (uint64_t)now.tv_sec * UINT64_C(1000000) +
        (uint64_t)now.tv_nsec / UINT64_C(1000);
    sources->tick_count = (uint64_t)clock();
    sources->application_id = (uint64_t)(uintptr_t)&code_marker;
    sources->application_name_hash = eos_port_hash_bytes("eos-abi-host");
    sources->thread_identity = (uint64_t)(uintptr_t)&stack_marker;
    sources->code_address = (uint64_t)(uintptr_t)&code_marker;
    sources->heap_address = (uint64_t)(uintptr_t)heap_marker;
    sources->stack_address = (uint64_t)(uintptr_t)&stack_marker;
    sources->device_diversifier = UINT64_C(0x7a796e712d686f73);
    sources->application_diversifier = UINT64_C(0x656f732d72757374);
    free(heap_marker);
}
