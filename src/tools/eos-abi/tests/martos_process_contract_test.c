#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eos_port_martos_process_contract.h"

typedef struct fixture {
    const char *names[8];
    int32_t name_status[8];
    int32_t delete_status[8];
    uint32_t delete_calls[8];
    int32_t count_status;
    uint32_t count_value;
    uint32_t allocation_calls;
    uint32_t fail_allocation_call;
    int32_t snapshot_status;
    uintptr_t snapshot_threads[8];
    uint32_t snapshot_count;
    uint32_t release_calls;
    uint32_t fail_release_call;
    uint32_t fail_fast_calls;
} fixture;

static void expect(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

static int32_t get_name(uintptr_t thread, char *name, uint32_t capacity,
                        void *opaque) {
    fixture *state = (fixture *)opaque;
    uint32_t index = (uint32_t)thread;
    if (state->name_status[index] != 0) return state->name_status[index];
    if (capacity == 0) return 77;
    strncpy(name, state->names[index], capacity - 1U);
    name[capacity - 1U] = '\0';
    return 0;
}

static int32_t delete_thread(uintptr_t thread, void *opaque) {
    fixture *state = (fixture *)opaque;
    uint32_t index = (uint32_t)thread;
    ++state->delete_calls[index];
    return state->delete_status[index];
}

static int32_t get_count(uint32_t *count, void *opaque) {
    fixture *state = (fixture *)opaque;
    *count = state->count_value;
    return state->count_status;
}

static int32_t allocate_snapshot(uint32_t bytes, void **memory, void *opaque) {
    fixture *state = (fixture *)opaque;
    ++state->allocation_calls;
    if (state->allocation_calls == state->fail_allocation_call) {
        *memory = NULL;
        return 15;
    }
    *memory = malloc(bytes);
    return *memory == NULL ? 15 : 0;
}

static int32_t snapshot_threads(void *storage, uint32_t capacity,
                                uintptr_t *threads, uint32_t *count,
                                void *opaque) {
    fixture *state = (fixture *)opaque;
    uint32_t index;
    (void)storage;
    if (state->snapshot_status != 0) return state->snapshot_status;
    if (state->snapshot_count > capacity) return 25;
    for (index = 0; index < state->snapshot_count; ++index) {
        threads[index] = state->snapshot_threads[index];
    }
    *count = state->snapshot_count;
    return 0;
}

static int32_t release_snapshot(void *memory, void *opaque) {
    fixture *state = (fixture *)opaque;
    ++state->release_calls;
    free(memory);
    return state->release_calls == state->fail_release_call ? 55 : 0;
}

static void fail_fast(void *opaque) {
    fixture *state = (fixture *)opaque;
    ++state->fail_fast_calls;
}

static void test_capability_gate(void) {
    expect(eos_martos_process_capabilities(UINT32_C(0x10)) ==
               UINT32_C(0x10),
           "real MARTOS capability selection must publish native stderr only");
    expect(eos_martos_process_options_supported(0, 0, 0, 0),
           "plain stdio-inheriting spawn must be supported");
    expect(!eos_martos_process_options_supported(1, 0, 0, 0),
           "target environment isolation must reject unsupported env");
    expect(!eos_martos_process_options_supported(0, 1, 0, 0),
           "target cwd isolation must reject unsupported cwd");
    expect(!eos_martos_process_options_supported(0, 0, 1, 0),
           "target stderr redirection must be rejected");
    expect(!eos_martos_process_options_supported(0, 0, 0, 1),
           "target arbitrary descriptor inheritance must be rejected");
}

static void test_redirection_result_mapping(void) {
    expect(eos_martos_process_read_result(1, 0, 32, 26) == 0,
           "one redirected input byte must map to OS_STS_OK");
    expect(eos_martos_process_read_result(0, 0, 32, 26) == 32,
           "redirected input EOF must map to OS_STS_END_OF_OBJECT");
    expect(eos_martos_process_read_result(-1, 0, 32, 26) == 26,
           "redirected input failure must map to device-read error");
    expect(eos_martos_process_write_result(1, 0, 27) == 0 &&
               eos_martos_process_write_result(0, 0, 27) == 27 &&
               eos_martos_process_write_result(-1, 0, 27) == 27,
           "redirected output completion mapping drifted");
}

static void test_exact_match_and_decoys(void) {
    const uintptr_t threads[] = {0, 1, 2, 3};
    fixture state = {0};
    uint32_t matched = 0;
    state.names[0] = "eos.rust.4.decoy";
    state.names[1] = "eos.rust.4";
    state.names[2] = "eos.rust";
    state.names[3] = "eos.rust.4";
    expect(eos_martos_process_kill_matching(
               threads, 4, "eos.rust.4", 64, 0, 42, get_name,
               delete_thread, &state, &matched) == 0 && matched == 2,
           "all and only exact live app names must be deleted");
    expect(state.delete_calls[0] == 0 && state.delete_calls[1] == 1 &&
               state.delete_calls[2] == 0 && state.delete_calls[3] == 1,
           "kill contract touched a prefix or decoy thread");
}

static void test_completion_races_and_error(void) {
    const uintptr_t threads[] = {0, 1, 2};
    fixture state = {0};
    uint32_t matched = 99;
    state.names[0] = "eos.rust.9";
    state.names[1] = "eos.rust.9";
    state.names[2] = "eos.rust.9";
    state.name_status[0] = 42;
    state.delete_status[1] = 42;
    state.delete_status[2] = 55;
    expect(eos_martos_process_kill_matching(
               threads, 2, "eos.rust.9", 64, 0, 42, get_name,
               delete_thread, &state, &matched) == 0 && matched == 0,
           "OBJECT_NOT_FOUND name/delete races must mean completion won");
    expect(state.delete_calls[0] == 0 && state.delete_calls[1] == 1,
           "completion race handling touched the wrong thread");
    matched = 99;
    expect(eos_martos_process_kill_matching(
               threads + 2, 1, "eos.rust.9", 64, 0, 42, get_name,
               delete_thread, &state, &matched) == 55 && matched == 0,
           "non-race delete failure must propagate deterministically");
}

static void test_partial_delete_failure_preserves_delivery(void) {
    const uintptr_t threads[] = {0, 1};
    fixture state = {0};
    uint32_t matched = 99;
    state.names[0] = "eos.rust.11";
    state.names[1] = "eos.rust.11";
    state.delete_status[1] = 55;
    expect(eos_martos_process_kill_matching(
               threads, 2, "eos.rust.11", 64, 0, 42, get_name,
               delete_thread, &state, &matched) == 55 && matched == 1,
           "a later delete failure must not erase an earlier delivered kill");
    expect(state.delete_calls[0] == 1 && state.delete_calls[1] == 1,
           "partial-delete fixture did not exercise both exact matches");
}

static int32_t enumerate(fixture *state, uint32_t *matched) {
    return eos_martos_process_enumerate_and_kill(
        "eos.rust.7", 64, 8, 0, 12, 15, get_count, allocate_snapshot,
        snapshot_threads, release_snapshot, get_name, delete_thread,
        fail_fast, state, matched);
}

static void test_snapshot_faults_and_cleanup(void) {
    fixture state = {0};
    uint32_t matched = 99;
    state.count_status = 25;
    expect(enumerate(&state, &matched) == 25 && matched == 0 &&
               state.allocation_calls == 0,
           "thread-count failure must propagate before allocation");

    memset(&state, 0, sizeof(state));
    state.count_value = 2;
    state.fail_allocation_call = 1;
    expect(enumerate(&state, &matched) == 15 && state.release_calls == 0,
           "status-snapshot allocation failure did not propagate");

    memset(&state, 0, sizeof(state));
    state.count_value = 2;
    state.fail_allocation_call = 2;
    expect(enumerate(&state, &matched) == 15 && state.release_calls == 1 &&
               state.fail_fast_calls == 0,
           "thread-vector allocation failure did not release status storage");

    memset(&state, 0, sizeof(state));
    state.count_value = 2;
    state.snapshot_status = 25;
    expect(enumerate(&state, &matched) == 25 && state.release_calls == 2 &&
               state.fail_fast_calls == 0,
           "thread-status failure did not release both snapshots");

    memset(&state, 0, sizeof(state));
    state.count_value = 1;
    state.snapshot_count = 1;
    state.snapshot_threads[0] = 0;
    state.names[0] = "eos.rust.7";
    state.fail_release_call = 1;
    expect(enumerate(&state, &matched) == 0 && matched == 1 &&
               state.release_calls == 2 && state.fail_fast_calls == 1,
           "irreversible snapshot release failure must invoke fail-fast");
}

static void test_unload_cleanup_policy(void) {
    expect(!eos_martos_process_cleanup_failed(0, 0) &&
               eos_martos_process_cleanup_failed(25, 0),
           "unload failure must be classified as fail-fast cleanup");
}

int main(void) {
    test_capability_gate();
    test_redirection_result_mapping();
    test_exact_match_and_decoys();
    test_completion_races_and_error();
    test_partial_delete_failure_preserves_delivery();
    test_snapshot_faults_and_cleanup();
    test_unload_cleanup_policy();
    return 0;
}
