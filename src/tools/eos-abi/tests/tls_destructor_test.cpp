#include "eos_rust_abi.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

extern "C" uint32_t eos_thread_test_live_records(void);
extern "C" void eos_tls_test_set_before_destructor_hook(
    void (*hook)(void *), void *context);

namespace {
[[noreturn]] void fail(const char *message) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
}
void expect(bool condition, const char *message) {
    if (!condition) fail(message);
}

std::mutex events_mutex;
std::vector<uintptr_t> events;
eos_rust_tls_key_t first_key;
eos_rust_tls_key_t second_key;
eos_rust_tls_key_t reinstall_key;
eos_rust_tls_key_t delete_key;
eos_rust_tls_key_t created_key;
std::atomic<int> reinstall_calls{0};
std::atomic<bool> cleanup_done{false};

struct DestructorGate {
    std::atomic<bool> selected{false};
    std::atomic<bool> release{false};
};

std::atomic<uint32_t> owned_destructor_calls{0};

void pause_selected_destructor(void *opaque) {
    auto *gate = static_cast<DestructorGate *>(opaque);
    gate->selected.store(true, std::memory_order_release);
    while (!gate->release.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
}

void owned_destructor(void *) { ++owned_destructor_calls; }

void record_destructor(void *value) {
    std::lock_guard<std::mutex> lock(events_mutex);
    events.push_back(reinterpret_cast<uintptr_t>(value));
}

void reinstall_destructor(void *value) {
    const int call = ++reinstall_calls;
    expect(eos_rust_pthread_getspecific(reinstall_key) == nullptr,
           "a destructor value must be cleared before callback");
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        events.push_back(reinterpret_cast<uintptr_t>(value));
    }
    (void)eos_rust_pthread_setspecific(
        reinstall_key, reinterpret_cast<void *>(static_cast<uintptr_t>(call + 1)));
}

void delete_and_create_destructor(void *value) {
    {
        std::lock_guard<std::mutex> lock(events_mutex);
        events.push_back(reinterpret_cast<uintptr_t>(value));
    }
    expect(eos_rust_pthread_key_delete(delete_key) == 0,
           "key deletion from destructor failed");
    expect(eos_rust_pthread_key_create(&created_key, record_destructor) == 0,
           "key creation from destructor failed");
    expect(eos_rust_pthread_setspecific(
        created_key, reinterpret_cast<void *>(static_cast<uintptr_t>(0x99U))) == 0,
           "new-key value from destructor failed");
}

void *destructor_worker(void *) {
    expect(eos_rust_pthread_setspecific(
               second_key, reinterpret_cast<void *>(static_cast<uintptr_t>(0x22U))) == 0 &&
               eos_rust_pthread_setspecific(
                   first_key, reinterpret_cast<void *>(static_cast<uintptr_t>(0x11U))) == 0 &&
               eos_rust_pthread_setspecific(
                   reinstall_key, reinterpret_cast<void *>(static_cast<uintptr_t>(1U))) == 0 &&
               eos_rust_pthread_setspecific(
                   delete_key, reinterpret_cast<void *>(static_cast<uintptr_t>(0x44U))) == 0,
           "destructor worker TLS setup failed");
    return reinterpret_cast<void *>(static_cast<uintptr_t>(0xfeedU));
}

void *mutation_worker(void *opaque) {
    auto controller_key = *static_cast<eos_rust_tls_key_t *>(opaque);
    expect(eos_rust_pthread_setspecific(
               controller_key, reinterpret_cast<void *>(static_cast<uintptr_t>(0x55U))) == 0 &&
               eos_rust_pthread_setspecific(
                   delete_key, reinterpret_cast<void *>(static_cast<uintptr_t>(0x66U))) == 0,
           "destructor mutation worker TLS setup failed");
    return nullptr;
}

void completion_destructor(void *) { cleanup_done.store(true); }

void *completion_worker(void *opaque) {
    auto key = *static_cast<eos_rust_tls_key_t *>(opaque);
    cleanup_done.store(false);
    expect(eos_rust_pthread_setspecific(key, reinterpret_cast<void *>(1)) == 0,
           "completion destructor setup failed");
    return reinterpret_cast<void *>(static_cast<uintptr_t>(0xbeefU));
}

void test_order_reinstallation_and_four_passes() {
    events.clear();
    reinstall_calls = 0;
    expect(eos_rust_pthread_key_create(&first_key, record_destructor) == 0 &&
               eos_rust_pthread_key_create(&second_key, record_destructor) == 0 &&
               eos_rust_pthread_key_create(&reinstall_key,
                                           reinstall_destructor) == 0 &&
               eos_rust_pthread_key_create(&delete_key, record_destructor) == 0,
           "destructor key creation failed");

    eos_rust_tls_key_t controller_key = 0;
    expect(eos_rust_pthread_key_create(&controller_key,
                                       delete_and_create_destructor) == 0,
           "controller key creation failed");

    eos_rust_thread_t thread = 0;
    void *result = nullptr;
    expect(eos_rust_pthread_create(&thread, nullptr, destructor_worker, nullptr) == 0,
           "destructor worker create failed");
    /* Install controller value inside another worker with direct shared setup omitted. */
    expect(eos_rust_pthread_join(thread, &result) == 0 &&
               result == reinterpret_cast<void *>(static_cast<uintptr_t>(0xfeedU)),
           "result publication must follow destructor completion");

    {
        std::lock_guard<std::mutex> lock(events_mutex);
        expect(events.size() >= 7 && events[0] == static_cast<uintptr_t>(0x11U) &&
                   events[1] == static_cast<uintptr_t>(0x22U) && events[2] == static_cast<uintptr_t>(1U),
               "destructors must begin in ascending key identity order");
        expect(reinstall_calls.load() == 4,
               "a reinstalled value must receive exactly four destructor passes");
        size_t reinstall_events = 0;
        for (uintptr_t value : events) {
            if (value >= 1 && value <= 4) ++reinstall_events;
        }
        expect(reinstall_events == 4,
               "the fifth reinstalled value must be discarded without a callback");
    }

    events.clear();
    expect(eos_rust_pthread_key_delete(delete_key) == 0 &&
               eos_rust_pthread_key_create(&delete_key, record_destructor) == 0 &&
               delete_key > controller_key,
           "mutation victim key must sort after its controller");
    eos_rust_thread_t mutation = 0;
    result = reinterpret_cast<void *>(1);
    expect(eos_rust_pthread_create(&mutation, nullptr, mutation_worker,
                                   &controller_key) == 0 &&
               eos_rust_pthread_join(mutation, &result) == 0 && result == nullptr,
           "destructor mutation worker failed");
    expect(created_key > controller_key,
           "a destructor-created key must get a later monotonic identity");
    expect(events.size() == 2 && events[0] == static_cast<uintptr_t>(0x55U) &&
               events[1] == static_cast<uintptr_t>(0x99U),
           "deleted keys must be skipped and destructor-created keys must run later in cleanup");
}

void test_cleanup_precedes_join_and_detach_reaps() {
    eos_rust_tls_key_t completion_key = 0;
    eos_rust_thread_t joinable = 0;
    eos_rust_thread_t detached = 0;
    void *result = nullptr;
    const uint32_t baseline = eos_thread_test_live_records();
    expect(eos_rust_pthread_key_create(&completion_key,
                                       completion_destructor) == 0,
           "completion key creation failed");
    expect(eos_rust_pthread_create(&joinable, nullptr, completion_worker,
                                   &completion_key) == 0 &&
               eos_rust_pthread_join(joinable, &result) == 0 &&
               cleanup_done.load() &&
               result == reinterpret_cast<void *>(static_cast<uintptr_t>(0xbeefU)),
           "join observed completion before TLS destructors");

    cleanup_done.store(false);
    expect(eos_rust_pthread_create(&detached, nullptr, completion_worker,
                                   &completion_key) == 0 &&
               eos_rust_pthread_detach(detached) == 0,
           "detached cleanup setup failed");
    for (int spin = 0; spin != 10000 &&
                       (!cleanup_done.load() ||
                        eos_thread_test_live_records() != baseline);
         ++spin) {
        std::this_thread::yield();
    }
    expect(cleanup_done.load() && eos_thread_test_live_records() == baseline,
           "detached thread did not run destructors and self-release its record");
}

void test_delete_after_destructor_selection_keeps_callback_alive() {
    eos_rust_tls_key_t key = 0;
    eos_rust_thread_t worker = 0;
    DestructorGate gate;
    owned_destructor_calls = 0;
    expect(eos_rust_pthread_key_create(&key, owned_destructor) == 0,
           "owned destructor key creation failed");
    eos_tls_test_set_before_destructor_hook(pause_selected_destructor, &gate);
    expect(eos_rust_pthread_create(&worker, nullptr, completion_worker, &key) == 0,
           "owned destructor worker creation failed");
    while (!gate.selected.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    expect(eos_rust_pthread_key_delete(key) == 0,
           "key deletion during selected destructor failed");
    gate.release.store(true, std::memory_order_release);
    expect(eos_rust_pthread_join(worker, nullptr) == 0,
           "owned destructor worker join failed");
    eos_tls_test_set_before_destructor_hook(nullptr, nullptr);
    expect(owned_destructor_calls.load() == 1,
           "an already-selected destructor must retain callback ownership");
}
} // namespace

int main() {
    test_order_reinstallation_and_four_passes();
    test_cleanup_precedes_join_and_detach_reaps();
    test_delete_after_destructor_selection_keeps_callback_alive();
    return 0;
}
