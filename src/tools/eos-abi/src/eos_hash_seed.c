#include "eos_splitmix64.h"

#include <stdint.h>
static uint64_t eos_hash_seed_state0;
static uint64_t eos_hash_seed_state1;
static uint64_t eos_hash_seed_counter;
static int eos_hash_seed_initialized;

#ifdef EOS_RUST_HOST_TEST
static eos_hash_seed_sources eos_hash_seed_injected_sources;
static int eos_hash_seed_sources_injected;
#endif

static int32_t eos_hash_seed_lock_acquire(void) {
    return eos_port_lock_acquire(EOS_PORT_LOCK_HASH_SEED);
}
static void eos_hash_seed_lock_release(void) {
    (void)eos_port_lock_release(EOS_PORT_LOCK_HASH_SEED);
}

static void eos_hash_seed_initialize(const eos_hash_seed_sources *sources) {
    const uint64_t values[] = {
        sources->timer_usec,
        sources->tick_count,
        sources->application_id,
        sources->application_name_hash,
        sources->thread_identity,
        sources->code_address,
        sources->heap_address,
        sources->stack_address,
        sources->device_diversifier,
        sources->application_diversifier,
    };
    uint32_t index;

    eos_hash_seed_state0 = UINT64_C(0x243f6a8885a308d3);
    eos_hash_seed_state1 = UINT64_C(0x13198a2e03707344);
    for (index = 0; index < (uint32_t)(sizeof(values) / sizeof(values[0]));
         ++index) {
        const uint64_t first_domain =
            UINT64_C(0x9e3779b97f4a7c15) * ((uint64_t)index + UINT64_C(1));
        const uint64_t second_domain =
            UINT64_C(0xd1b54a32d192ed03) +
            UINT64_C(0x94d049bb133111eb) * (uint64_t)index;
        eos_hash_seed_state0 =
            eos_splitmix64(eos_hash_seed_state0 ^ values[index] ^ first_domain);
        eos_hash_seed_state1 =
            eos_splitmix64((eos_hash_seed_state1 + values[index]) ^
                           second_domain);
    }
    eos_hash_seed_counter = 0;
    eos_hash_seed_initialized = 1;
}

void eos_rust_hash_seed(uint64_t *key0, uint64_t *key1) {
    eos_hash_seed_sources sources;
    uint64_t output0;
    uint64_t output1;

    int32_t lock_status = eos_hash_seed_lock_acquire();
    if (lock_status != EOS_PORT_STATUS_OK) {
        eos_allocation_record_failure(lock_status, "hash_seed.lock");
        return;
    }
    if (!eos_hash_seed_initialized) {
#ifdef EOS_RUST_HOST_TEST
        if (eos_hash_seed_sources_injected) {
            sources = eos_hash_seed_injected_sources;
        } else {
            eos_port_hash_seed_sources(&sources);
        }
#else
        eos_port_hash_seed_sources(&sources);
#endif
        eos_hash_seed_initialize(&sources);
    }

    ++eos_hash_seed_counter;
    eos_hash_seed_state0 = eos_splitmix64(
        eos_hash_seed_state0 ^ eos_hash_seed_counter ^
        UINT64_C(0xa0761d6478bd642f));
    eos_hash_seed_state1 = eos_splitmix64(
        eos_hash_seed_state1 + eos_hash_seed_state0 +
        (eos_hash_seed_counter ^ UINT64_C(0xe7037ed1a0b428db)));
    output0 = eos_splitmix64(eos_hash_seed_state0 ^ eos_hash_seed_counter ^
                             UINT64_C(0x8ebc6af09c88c6e3));
    output1 = eos_splitmix64(eos_hash_seed_state1 ^ eos_hash_seed_counter ^
                             UINT64_C(0x589965cc75374cc3));
    if (key0 != NULL) {
        *key0 = output0;
    }
    if (key1 != NULL) {
        *key1 = output1;
    }
    eos_hash_seed_lock_release();
}

#ifdef EOS_RUST_HOST_TEST
void eos_hash_seed_test_reset(const eos_hash_seed_sources *sources) {
    int32_t lock_status = eos_hash_seed_lock_acquire();
    if (lock_status != EOS_PORT_STATUS_OK) return;
    eos_hash_seed_injected_sources = *sources;
    eos_hash_seed_sources_injected = 1;
    eos_hash_seed_initialized = 0;
    eos_hash_seed_state0 = 0;
    eos_hash_seed_state1 = 0;
    eos_hash_seed_counter = 0;
    eos_hash_seed_lock_release();
}
#endif
