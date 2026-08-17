#include "eos_rust_abi.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#ifndef EOS_RUST_TSAN_TEST
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

extern "C" {
void eos_host_test_reset(void);
void eos_host_test_fail_next_alloc(int32_t status);
void eos_host_test_fail_next_lock(int32_t status);
void eos_host_test_fail_next_public_mutex_create(int32_t status);
void eos_host_test_fail_next_public_mutex_lock(int32_t status);
void eos_host_test_fail_next_public_mutex_unlock(int32_t status);
void eos_host_test_fail_next_public_mutex_delete(int32_t status);
void eos_host_test_fail_next_public_sem_create(int32_t status);
void eos_host_test_fail_next_public_sem_take(int32_t status);
void eos_host_test_spurious_next_public_sem_take(void);
void eos_host_test_fail_next_public_sem_give(int32_t status);
void eos_host_test_fail_next_public_sem_delete(int32_t status);
uint32_t eos_sync_test_last_mutex_kind(void);
uint32_t eos_sync_test_live_mutexes(void);
uint32_t eos_sync_test_live_conditions(void);
uint32_t eos_sync_test_live_rwlocks(void);
uint32_t eos_sync_test_live_once(void);
void eos_sync_test_exhaust_identities(void);
void eos_sync_test_pause_after_condition_timeout(int enabled);
uint32_t eos_sync_test_condition_timeout_pause_entered(void);
void eos_sync_test_resume_after_condition_timeout(void);
void eos_sync_test_reset_rwlock_wait_audit(void);
uint32_t eos_sync_test_rwlock_writer_wait_entered(void);
}

namespace {
constexpr int32_t kIo = 5;
constexpr int32_t kNoMemory = 12;
constexpr int32_t kBusy = 16;
constexpr int32_t kInvalid = 22;
constexpr int32_t kNotSupported = 45;
constexpr int32_t kTimedOut = 60;

[[noreturn]] void fail(const char *message) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const char *message) {
    if (!condition) fail(message);
}

eos_rust_timespec deadline_after(int64_t nanoseconds) {
    eos_rust_timespec value{};
    expect(eos_rust_clock_gettime(EOS_RUST_CLOCK_MONOTONIC, &value) == 0,
           "reading a monotonic deadline failed");
    value.tv_sec += nanoseconds / INT64_C(1000000000);
    value.tv_nsec += nanoseconds % INT64_C(1000000000);
    if (value.tv_nsec >= INT64_C(1000000000)) {
        ++value.tv_sec;
        value.tv_nsec -= INT64_C(1000000000);
    }
    return value;
}

#ifndef EOS_RUST_TSAN_TEST
template <typename Function>
void expect_blocks(Function function, const char *message) {
    const pid_t child = fork();
    expect(child >= 0, "fork failed");
    if (child == 0) {
        (void)signal(SIGALRM, SIG_DFL);
        alarm(1);
        function();
        _exit(EXIT_SUCCESS);
    }
    int status = 0;
    expect(waitpid(child, &status, 0) == child, "waitpid failed");
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM, message);
}

template <typename Function>
void expect_aborts(Function function, const char *message) {
    const pid_t child = fork();
    expect(child >= 0, "fork failed");
    if (child == 0) {
        function();
        _exit(EXIT_SUCCESS);
    }
    int status = 0;
    expect(waitpid(child, &status, 0) == child, "waitpid failed");
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, message);
}
#endif

void test_layouts_and_attributes() {
    static_assert(sizeof(eos_rust_timespec) == 16);
    static_assert(alignof(eos_rust_timespec) == alignof(int64_t));
    static_assert(sizeof(eos_rust_pthread_mutex) == 16);
    static_assert(offsetof(eos_rust_pthread_mutex, words) == 0);
    static_assert(sizeof(eos_rust_pthread_mutexattr) == 8);
    static_assert(offsetof(eos_rust_pthread_mutexattr, words) == 0);
    static_assert(sizeof(eos_rust_pthread_cond) == 16);
    static_assert(offsetof(eos_rust_pthread_cond, words) == 0);
    static_assert(sizeof(eos_rust_pthread_condattr) == 8);
    static_assert(offsetof(eos_rust_pthread_condattr, words) == 0);
    static_assert(sizeof(eos_rust_pthread_rwlock) == 16);
    static_assert(offsetof(eos_rust_pthread_rwlock, words) == 0);
    static_assert(sizeof(eos_rust_pthread_once_t) == 8);
    static_assert(offsetof(eos_rust_pthread_once_t, words) == 0);
    static_assert(EOS_RUST_CLOCK_REALTIME == 0);
    static_assert(EOS_RUST_CLOCK_MONOTONIC == 1);
    static_assert(EOS_RUST_PTHREAD_MUTEX_NORMAL == 0);
    static_assert(EOS_RUST_PTHREAD_MUTEX_RECURSIVE == 1);

    eos_rust_pthread_mutexattr attribute{};
    expect(eos_rust_pthread_mutexattr_settype(
               &attribute, EOS_RUST_PTHREAD_MUTEX_RECURSIVE) == 0,
           "all-zero mutex attributes must accept recursive type");
    expect(eos_rust_pthread_mutexattr_settype(&attribute, 77) ==
               kNotSupported,
           "unsupported mutex types must be honest");
    expect(eos_rust_pthread_mutexattr_destroy(&attribute) == 0,
           "mutex attribute destroy failed");
    const auto destroyed_attribute = attribute;
    expect(eos_rust_pthread_mutexattr_settype(
               &attribute, EOS_RUST_PTHREAD_MUTEX_NORMAL) == kInvalid &&
               eos_rust_pthread_mutexattr_settype(&attribute, 77) == kInvalid &&
               std::memcmp(&attribute, &destroyed_attribute,
                           sizeof(attribute)) == 0,
           "malformed mutex attributes must take precedence and remain unchanged");
    expect(eos_rust_pthread_mutexattr_init(&attribute) == 0,
           "mutex attribute init failed");

    eos_rust_pthread_condattr condition_attribute{};
    expect(eos_rust_pthread_condattr_setclock(
               &condition_attribute, EOS_RUST_CLOCK_MONOTONIC) == 0,
           "monotonic condition attributes must be accepted");
    expect(eos_rust_pthread_condattr_setclock(
               &condition_attribute, EOS_RUST_CLOCK_REALTIME) ==
               kNotSupported,
           "realtime condition waits must not silently use monotonic time");
    expect(eos_rust_pthread_condattr_setclock(&condition_attribute, 9) ==
               kInvalid,
           "unknown condition clocks must be invalid");
    expect(eos_rust_pthread_condattr_destroy(&condition_attribute) == 0,
           "condition attribute destroy failed");
    expect(eos_rust_pthread_condattr_init(&condition_attribute) == 0,
           "condition attribute init failed");

    eos_rust_pthread_mutex untouched_mutex{};
    eos_rust_pthread_cond untouched_condition{};
    eos_rust_pthread_rwlock untouched_rwlock{};
    expect(eos_rust_pthread_mutex_destroy(&untouched_mutex) == 0 &&
               eos_rust_pthread_cond_destroy(&untouched_condition) == 0 &&
               eos_rust_pthread_rwlock_destroy(&untouched_rwlock) == 0,
           "destroying untouched lazy objects must succeed");
    expect(untouched_mutex.words[0] == 0 && untouched_condition.words[0] == 0 &&
               untouched_rwlock.words[0] == 0,
           "destroying untouched objects must leave the zero representation");

    eos_rust_pthread_mutex malformed_mutex{{1, 0, 0, 0}};
    eos_rust_pthread_cond malformed_condition{{1, 0, 0, 0}};
    eos_rust_pthread_rwlock malformed_rwlock{{1, 0, 0, 0}};
    const auto original_mutex = malformed_mutex;
    const auto original_condition = malformed_condition;
    const auto original_rwlock = malformed_rwlock;
    expect(eos_rust_pthread_mutex_init(&malformed_mutex, nullptr) == kInvalid &&
               eos_rust_pthread_cond_init(&malformed_condition, nullptr) ==
                   kInvalid &&
               eos_rust_pthread_rwlock_init(&malformed_rwlock) == kInvalid &&
               std::memcmp(&malformed_mutex, &original_mutex,
                           sizeof(malformed_mutex)) == 0 &&
               std::memcmp(&malformed_condition, &original_condition,
                           sizeof(malformed_condition)) == 0 &&
               std::memcmp(&malformed_rwlock, &original_rwlock,
                           sizeof(malformed_rwlock)) == 0,
           "explicit init must reject malformed nonzero objects without mutation");
}

void test_mutexes() {
    eos_rust_pthread_mutex mutex{};
    std::atomic<uint32_t> counter{0};
    std::vector<std::thread> workers;
    for (uint32_t index = 0; index != 16; ++index) {
        workers.emplace_back([&] {
            for (uint32_t iteration = 0; iteration != 1000; ++iteration) {
                expect(eos_rust_pthread_mutex_lock(&mutex) == 0,
                       "lazy mutex lock failed");
                counter.store(counter.load(std::memory_order_relaxed) + 1,
                              std::memory_order_relaxed);
                expect(eos_rust_pthread_mutex_unlock(&mutex) == 0,
                       "lazy mutex unlock failed");
            }
        });
    }
    for (auto &worker : workers) worker.join();
    expect(counter.load() == 16000 && mutex.words[0] != 0,
           "concurrent lazy mutex publication lost an update");
    expect(eos_sync_test_last_mutex_kind() ==
               EOS_RUST_PTHREAD_MUTEX_NORMAL,
           "all-zero mutex must map to a native normal mutex");
    expect(eos_rust_pthread_mutex_lock(&mutex) == 0 &&
               eos_rust_pthread_mutex_trylock(&mutex) == kBusy &&
               eos_rust_pthread_mutex_destroy(&mutex) == kBusy &&
               eos_rust_pthread_mutex_unlock(&mutex) == 0,
           "normal mutex busy/try/destroy semantics drifted");
    expect(eos_rust_pthread_mutex_destroy(&mutex) == 0,
           "unlocked mutex destruction failed");
    const auto stale = mutex;
    expect(eos_rust_pthread_mutex_lock(&mutex) == kInvalid &&
               std::memcmp(&mutex, &stale, sizeof(mutex)) == 0,
           "destroyed mutex must be stale and unchanged");

    eos_rust_pthread_mutexattr recursive_attribute{};
    eos_rust_pthread_mutex recursive{};
    expect(eos_rust_pthread_mutexattr_settype(
               &recursive_attribute, EOS_RUST_PTHREAD_MUTEX_RECURSIVE) == 0 &&
               eos_rust_pthread_mutex_init(&recursive, &recursive_attribute) ==
                   0 &&
               eos_sync_test_last_mutex_kind() ==
                   EOS_RUST_PTHREAD_MUTEX_RECURSIVE &&
               eos_rust_pthread_mutex_lock(&recursive) == 0 &&
               eos_rust_pthread_mutex_lock(&recursive) == 0 &&
               eos_rust_pthread_mutex_unlock(&recursive) == 0 &&
               eos_rust_pthread_mutex_destroy(&recursive) == kBusy &&
               eos_rust_pthread_mutex_unlock(&recursive) == 0 &&
               eos_rust_pthread_mutex_destroy(&recursive) == 0,
           "recursive mutex mapping/depth semantics drifted");

#ifndef EOS_RUST_TSAN_TEST
    expect_blocks([] {
        eos_rust_pthread_mutex normal{};
        if (eos_rust_pthread_mutex_lock(&normal) != 0) _exit(2);
        (void)eos_rust_pthread_mutex_lock(&normal);
    }, "a normal mutex relock by its owner must block");
#endif

    eos_rust_pthread_mutex faulted{};
    eos_host_test_fail_next_public_mutex_create(15);
    expect(eos_rust_pthread_mutex_lock(&faulted) == kNoMemory &&
               faulted.words[0] == 0,
           "native mutex creation failure must not publish an object");
    eos_host_test_fail_next_public_mutex_lock(25);
    expect(eos_rust_pthread_mutex_lock(&faulted) == kIo &&
               faulted.words[0] != 0,
           "native lock failure must leave a live balanced record");
    expect(eos_rust_pthread_mutex_destroy(&faulted) == 0,
           "mutex must remain destroyable after failed acquisition");

    eos_rust_pthread_mutex wrong_owner{};
    expect(eos_rust_pthread_mutex_lock(&wrong_owner) == 0,
           "wrong-owner mutex setup failed");
    std::atomic<int32_t> wrong_owner_status{0};
    std::thread wrong_owner_thread([&] {
        wrong_owner_status.store(eos_rust_pthread_mutex_unlock(&wrong_owner),
                                 std::memory_order_release);
    });
    wrong_owner_thread.join();
    expect(wrong_owner_status.load(std::memory_order_acquire) == kInvalid &&
               eos_rust_pthread_mutex_unlock(&wrong_owner) == 0 &&
               eos_rust_pthread_mutex_destroy(&wrong_owner) == 0,
           "wrong-owner mutex unlock must be rejected without state loss");

#ifndef EOS_RUST_TSAN_TEST
    expect_aborts([] {
        eos_rust_pthread_mutex value{};
        if (eos_rust_pthread_mutex_lock(&value) != 0) _exit(2);
        eos_host_test_fail_next_public_mutex_unlock(25);
        (void)eos_rust_pthread_mutex_unlock(&value);
    }, "irreversible native mutex unlock failure must abort");
    expect_aborts([] {
        eos_rust_pthread_mutex value{};
        if (eos_rust_pthread_mutex_lock(&value) != 0 ||
            eos_rust_pthread_mutex_unlock(&value) != 0) {
            _exit(2);
        }
        eos_host_test_fail_next_public_mutex_delete(25);
        (void)eos_rust_pthread_mutex_destroy(&value);
    }, "irreversible native mutex delete failure must abort");
#endif
}

void test_concurrent_explicit_initialization() {
    eos_rust_pthread_mutex mutex{};
    eos_rust_pthread_cond condition{};
    eos_rust_pthread_rwlock rwlock{};
    std::atomic<uint32_t> mutex_success{0};
    std::atomic<uint32_t> condition_success{0};
    std::atomic<uint32_t> rwlock_success{0};
    std::vector<std::thread> initializers;
    for (uint32_t index = 0; index != 16; ++index) {
        initializers.emplace_back([&] {
            const int32_t mutex_status =
                eos_rust_pthread_mutex_init(&mutex, nullptr);
            const int32_t condition_status =
                eos_rust_pthread_cond_init(&condition, nullptr);
            const int32_t rwlock_status = eos_rust_pthread_rwlock_init(&rwlock);
            if (mutex_status == 0) ++mutex_success;
            else expect(mutex_status == kBusy || mutex_status == kInvalid,
                        "concurrent mutex init returned an unexpected error");
            if (condition_status == 0) ++condition_success;
            else expect(condition_status == kBusy || condition_status == kInvalid,
                        "concurrent condition init returned an unexpected error");
            if (rwlock_status == 0) ++rwlock_success;
            else expect(rwlock_status == kBusy || rwlock_status == kInvalid,
                        "concurrent rwlock init returned an unexpected error");
        });
    }
    for (auto &initializer : initializers) initializer.join();
    expect(mutex_success.load() == 1 && condition_success.load() == 1 &&
               rwlock_success.load() == 1,
           "exactly one concurrent explicit initializer must publish");
    expect(eos_rust_pthread_mutex_destroy(&mutex) == 0 &&
               eos_rust_pthread_cond_destroy(&condition) == 0 &&
               eos_rust_pthread_rwlock_destroy(&rwlock) == 0,
           "explicit initializer cleanup failed");
}

void test_conditions_and_parker() {
    eos_rust_pthread_mutex mutex{};
    eos_rust_pthread_cond condition{};
    bool released = false;
    std::atomic<uint32_t> waiting{0};
    std::atomic<uint32_t> wait_returns{0};
    std::atomic<uint32_t> woke{0};
    std::vector<std::thread> waiters;
    for (uint32_t index = 0; index != 8; ++index) {
        waiters.emplace_back([&] {
            expect(eos_rust_pthread_mutex_lock(&mutex) == 0,
                   "condition waiter mutex lock failed");
            waiting.fetch_add(1, std::memory_order_release);
            while (!released) {
                expect(eos_rust_pthread_cond_wait(&condition, &mutex) == 0,
                       "condition wait failed");
                wait_returns.fetch_add(1, std::memory_order_release);
            }
            woke.fetch_add(1, std::memory_order_relaxed);
            expect(eos_rust_pthread_mutex_unlock(&mutex) == 0,
                   "condition waiter mutex unlock failed");
        });
    }
    while (waiting.load(std::memory_order_acquire) != 8) std::this_thread::yield();
    expect(eos_rust_pthread_cond_destroy(&condition) == kBusy,
           "condition destruction with waiters must be busy");
    expect(eos_rust_pthread_mutex_lock(&mutex) == 0,
           "condition broadcaster mutex lock failed");
    expect(eos_rust_pthread_cond_signal(&condition) == 0,
           "condition signal failed");
    expect(eos_rust_pthread_mutex_unlock(&mutex) == 0,
           "condition broadcaster mutex unlock failed");
    for (uint32_t spin = 0; spin != 100000 &&
                            wait_returns.load(std::memory_order_acquire) == 0;
         ++spin) {
        std::this_thread::yield();
    }
    expect(wait_returns.load(std::memory_order_acquire) == 1 &&
               woke.load() == 0,
           "signal must select exactly one current waiter");
    expect(eos_rust_pthread_mutex_lock(&mutex) == 0,
           "condition broadcast mutex lock failed");
    released = true;
    expect(eos_rust_pthread_cond_broadcast(&condition) == 0,
           "condition broadcast failed");
    expect(eos_rust_pthread_mutex_unlock(&mutex) == 0,
           "condition broadcast unlock failed");
    for (auto &waiter : waiters) waiter.join();
    expect(woke.load() == 8, "broadcast stranded a current waiter");

    /* Parker token: notify-before-wait is represented by the mutex predicate. */
    bool token = true;
    expect(eos_rust_pthread_mutex_lock(&mutex) == 0,
           "parker token mutex lock failed");
    if (!token) {
        expect(eos_rust_pthread_cond_wait(&condition, &mutex) == 0,
               "parker consumed a missing token");
    }
    token = false;
    expect(eos_rust_pthread_mutex_unlock(&mutex) == 0,
           "parker token mutex unlock failed");

    expect(eos_rust_pthread_mutex_lock(&mutex) == 0,
           "timeout mutex lock failed");
    const auto expired = deadline_after(-1);
    expect(eos_rust_pthread_cond_timedwait(&condition, &mutex, &expired) ==
               kTimedOut,
           "expired condition deadline must time out");
    expect(eos_rust_pthread_mutex_trylock(&mutex) == kBusy,
           "timed wait must reacquire the user mutex before returning");
    expect(eos_rust_pthread_mutex_unlock(&mutex) == 0,
           "timeout mutex unlock failed");

    eos_rust_timespec malformed{0, INT64_C(1000000000)};
    expect(eos_rust_pthread_mutex_lock(&mutex) == 0,
           "malformed timeout mutex lock failed");
    expect(eos_rust_pthread_cond_timedwait(&condition, &mutex, &malformed) ==
               kInvalid &&
               eos_rust_pthread_mutex_trylock(&mutex) == kBusy,
           "invalid deadlines must preserve user-mutex ownership");
    expect(eos_rust_pthread_mutex_unlock(&mutex) == 0,
           "malformed timeout mutex unlock failed");

    eos_rust_pthread_cond future{};
    expect(eos_rust_pthread_cond_signal(&future) == 0,
           "signal with no current waiters failed");
    expect(eos_rust_pthread_mutex_lock(&mutex) == 0,
           "future waiter mutex lock failed");
    const auto short_deadline = deadline_after(INT64_C(2000000));
    expect(eos_rust_pthread_cond_timedwait(&future, &mutex, &short_deadline) ==
               kTimedOut,
           "a future waiter stole an old signal");
    expect(eos_rust_pthread_mutex_unlock(&mutex) == 0,
           "future waiter mutex unlock failed");

    eos_rust_pthread_cond race_condition{};
    eos_rust_pthread_mutex race_mutex{};
    std::atomic<int32_t> race_result{kInvalid};
    eos_sync_test_pause_after_condition_timeout(1);
    std::thread timeout_racer([&] {
        expect(eos_rust_pthread_mutex_lock(&race_mutex) == 0,
               "timeout race mutex lock failed");
        const auto race_deadline = deadline_after(-1);
        race_result.store(eos_rust_pthread_cond_timedwait(
                              &race_condition, &race_mutex, &race_deadline),
                          std::memory_order_release);
        expect(eos_rust_pthread_mutex_unlock(&race_mutex) == 0,
               "timeout race mutex unlock failed");
    });
    while (eos_sync_test_condition_timeout_pause_entered() == 0) {
        std::this_thread::yield();
    }
    expect(eos_rust_pthread_cond_signal(&race_condition) == 0,
           "timeout-race signal failed");
    eos_sync_test_resume_after_condition_timeout();
    timeout_racer.join();
    expect(race_result.load(std::memory_order_acquire) == 0,
           "selected signal must win the deterministic timeout race");

    bool later_token = false;
    expect(eos_rust_pthread_mutex_lock(&race_mutex) == 0,
           "later-token mutex lock failed");
    const auto later_expired = deadline_after(-1);
    expect(eos_rust_pthread_cond_timedwait(
               &race_condition, &race_mutex, &later_expired) == kTimedOut,
           "a timeout must not manufacture a signal token");
    later_token = true;
    expect(eos_rust_pthread_cond_signal(&race_condition) == 0,
           "later notification failed");
    if (!later_token) {
        fail("a timeout consumed a later predicate notification");
    }
    later_token = false;
    expect(eos_rust_pthread_mutex_unlock(&race_mutex) == 0,
           "later-token mutex unlock failed");

    eos_rust_pthread_cond fault_condition{};
    eos_host_test_fail_next_public_mutex_create(15);
    expect(eos_rust_pthread_cond_signal(&fault_condition) == kNoMemory &&
               fault_condition.words[0] == 0,
           "condition native-lock creation failure must not publish");
    expect(eos_rust_pthread_cond_signal(&fault_condition) == 0,
           "condition fault setup failed");
    eos_host_test_fail_next_public_mutex_lock(25);
    expect(eos_rust_pthread_cond_signal(&fault_condition) == kIo,
           "condition internal-lock failure must map and balance ownership");

    eos_rust_pthread_cond fault_wait_condition{};
    eos_rust_pthread_mutex fault_wait_mutex{};
    expect(eos_rust_pthread_mutex_lock(&fault_wait_mutex) == 0,
           "condition fault waiter mutex lock failed");
    eos_host_test_fail_next_public_sem_create(15);
    expect(eos_rust_pthread_cond_wait(&fault_wait_condition,
                                      &fault_wait_mutex) == kNoMemory &&
               eos_rust_pthread_mutex_trylock(&fault_wait_mutex) == kBusy,
           "waiter semaphore creation failure must preserve mutex ownership");
    eos_host_test_fail_next_public_sem_take(25);
    expect(eos_rust_pthread_cond_wait(&fault_wait_condition,
                                      &fault_wait_mutex) == kIo &&
               eos_rust_pthread_mutex_trylock(&fault_wait_mutex) == kBusy,
           "condition wait failure must reacquire the user mutex");
    eos_host_test_spurious_next_public_sem_take();
    expect(eos_rust_pthread_cond_wait(&fault_wait_condition,
                                      &fault_wait_mutex) == 0 &&
               eos_rust_pthread_mutex_trylock(&fault_wait_mutex) == kBusy,
           "a spurious wake must preserve waiter accounting and mutex ownership");
    expect(eos_rust_pthread_mutex_unlock(&fault_wait_mutex) == 0,
           "condition fault waiter mutex unlock failed");

#ifndef EOS_RUST_TSAN_TEST
    expect_aborts([] {
        eos_rust_pthread_cond value{};
        eos_rust_pthread_mutex mutex_value{};
        if (eos_rust_pthread_mutex_lock(&mutex_value) != 0) _exit(2);
        eos_host_test_fail_next_public_sem_delete(25);
        const auto expired_value = deadline_after(-1);
        (void)eos_rust_pthread_cond_timedwait(
            &value, &mutex_value, &expired_value);
    }, "irreversible waiter semaphore delete failure must abort");
    expect_aborts([] {
        eos_rust_pthread_cond value{};
        eos_rust_pthread_mutex mutex_value{};
        eos_sync_test_pause_after_condition_timeout(1);
        std::thread waiter([&] {
            if (eos_rust_pthread_mutex_lock(&mutex_value) != 0) _exit(2);
            const auto expired_value = deadline_after(-1);
            (void)eos_rust_pthread_cond_timedwait(
                &value, &mutex_value, &expired_value);
        });
        while (eos_sync_test_condition_timeout_pause_entered() == 0) {
            std::this_thread::yield();
        }
        eos_host_test_fail_next_public_sem_give(25);
        (void)eos_rust_pthread_cond_signal(&value);
        waiter.join();
    }, "failed condition wake after selection must abort");
#endif

    expect(eos_rust_pthread_cond_destroy(&future) == 0 &&
               eos_rust_pthread_cond_destroy(&condition) == 0 &&
               eos_rust_pthread_cond_destroy(&race_condition) == 0 &&
               eos_rust_pthread_mutex_destroy(&race_mutex) == 0 &&
               eos_rust_pthread_cond_destroy(&fault_condition) == 0 &&
               eos_rust_pthread_cond_destroy(&fault_wait_condition) == 0 &&
               eos_rust_pthread_mutex_destroy(&fault_wait_mutex) == 0 &&
               eos_rust_pthread_mutex_destroy(&mutex) == 0,
           "condition/parker cleanup failed");
}

void test_rwlock_writer_preference() {
    eos_rust_pthread_rwlock rwlock{};
    expect(eos_rust_pthread_rwlock_rdlock(&rwlock) == 0 &&
               eos_rust_pthread_rwlock_trywrlock(&rwlock) == kBusy &&
               eos_rust_pthread_rwlock_destroy(&rwlock) == kBusy,
           "rwlock reader/try-writer/busy-destroy semantics drifted");
    expect(eos_rust_pthread_rwlock_tryrdlock(&rwlock) == 0,
           "rwlock must admit concurrent readers");
    expect(eos_rust_pthread_rwlock_unlock(&rwlock) == 0 &&
               eos_rust_pthread_rwlock_unlock(&rwlock) == 0,
           "rwlock reader unlock failed");

    expect(eos_rust_pthread_rwlock_rdlock(&rwlock) == 0,
           "rwlock preference setup failed");
    std::atomic<int> order{0};
    eos_sync_test_reset_rwlock_wait_audit();
    std::thread writer([&] {
        expect(eos_rust_pthread_rwlock_wrlock(&rwlock) == 0,
               "queued writer failed");
        expect(order.fetch_add(1) == 0,
               "a late reader bypassed an already queued writer");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        expect(eos_rust_pthread_rwlock_unlock(&rwlock) == 0,
               "writer unlock failed");
    });
    while (eos_sync_test_rwlock_writer_wait_entered() == 0) {
        std::this_thread::yield();
    }
    std::thread late_reader([&] {
        expect(eos_rust_pthread_rwlock_rdlock(&rwlock) == 0,
               "late reader failed");
        expect(order.fetch_add(1) == 1,
               "writer preference order drifted");
        expect(eos_rust_pthread_rwlock_unlock(&rwlock) == 0,
               "late reader unlock failed");
    });
    expect(eos_rust_pthread_rwlock_unlock(&rwlock) == 0,
           "preference setup reader unlock failed");
    writer.join();
    late_reader.join();
    expect(order.load() == 2 && eos_rust_pthread_rwlock_destroy(&rwlock) == 0,
           "rwlock writer-preference cleanup failed");
}

void test_rwlock_faults_and_contention() {
    eos_rust_pthread_rwlock allocation_fault{};
    eos_host_test_fail_next_public_sem_create(15);
    expect(eos_rust_pthread_rwlock_rdlock(&allocation_fault) == kNoMemory &&
               allocation_fault.words[0] == 0,
           "rwlock semaphore creation failure must not publish");

    eos_rust_pthread_rwlock wait_fault{};
    expect(eos_rust_pthread_rwlock_rdlock(&wait_fault) == 0,
           "rwlock wait-fault reader setup failed");
    eos_host_test_fail_next_public_sem_take(25);
    expect(eos_rust_pthread_rwlock_wrlock(&wait_fault) == kIo,
           "rwlock wait failure must map and roll back writer accounting");
    expect(eos_rust_pthread_rwlock_unlock(&wait_fault) == 0,
           "rwlock wait-fault reader unlock failed");
    eos_host_test_fail_next_public_mutex_lock(25);
    expect(eos_rust_pthread_rwlock_tryrdlock(&wait_fault) == kIo,
           "rwlock internal-lock failure must map and balance ownership");
    expect(eos_rust_pthread_rwlock_destroy(&wait_fault) == 0,
           "rwlock fault rollback left the record busy");

#ifndef EOS_RUST_TSAN_TEST
    expect_aborts([] {
        eos_rust_pthread_rwlock value{};
        if (eos_rust_pthread_rwlock_rdlock(&value) != 0) _exit(2);
        eos_sync_test_reset_rwlock_wait_audit();
        std::thread writer([&] {
            (void)eos_rust_pthread_rwlock_wrlock(&value);
        });
        while (eos_sync_test_rwlock_writer_wait_entered() == 0) {
            std::this_thread::yield();
        }
        eos_host_test_fail_next_public_sem_give(25);
        (void)eos_rust_pthread_rwlock_unlock(&value);
        writer.join();
    }, "failed rwlock wake after ownership transfer must abort");
    expect_aborts([] {
        eos_rust_pthread_rwlock value{};
        if (eos_rust_pthread_rwlock_rdlock(&value) != 0 ||
            eos_rust_pthread_rwlock_unlock(&value) != 0) {
            _exit(2);
        }
        eos_host_test_fail_next_public_sem_delete(25);
        (void)eos_rust_pthread_rwlock_destroy(&value);
    }, "irreversible rwlock semaphore deletion failure must abort");
#endif

    eos_rust_pthread_rwlock contended{};
    std::atomic<uint32_t> active_readers{0};
    std::atomic<uint32_t> active_writers{0};
    std::atomic<uint32_t> violations{0};
    std::atomic<uint32_t> writes{0};
    std::vector<std::thread> workers;
    for (uint32_t index = 0; index != 4; ++index) {
        workers.emplace_back([&] {
            for (uint32_t iteration = 0; iteration != 200; ++iteration) {
                if (eos_rust_pthread_rwlock_wrlock(&contended) != 0) {
                    ++violations;
                    return;
                }
                if (active_writers.fetch_add(1) != 0 ||
                    active_readers.load() != 0) {
                    ++violations;
                }
                ++writes;
                if (active_writers.fetch_sub(1) != 1) ++violations;
                if (eos_rust_pthread_rwlock_unlock(&contended) != 0) {
                    ++violations;
                    return;
                }
            }
        });
    }
    for (uint32_t index = 0; index != 8; ++index) {
        workers.emplace_back([&] {
            for (uint32_t iteration = 0; iteration != 500; ++iteration) {
                if (eos_rust_pthread_rwlock_rdlock(&contended) != 0) {
                    ++violations;
                    return;
                }
                active_readers.fetch_add(1);
                if (active_writers.load() != 0) ++violations;
                active_readers.fetch_sub(1);
                if (eos_rust_pthread_rwlock_unlock(&contended) != 0) {
                    ++violations;
                    return;
                }
            }
        });
    }
    for (auto &worker : workers) worker.join();
    expect(violations.load() == 0 && writes.load() == 800 &&
               eos_rust_pthread_rwlock_destroy(&contended) == 0,
           "rwlock contention violated reader/writer exclusion");
}

std::atomic<uint32_t> once_calls{0};
std::atomic<uint32_t> once_visible{0};
std::atomic<bool> once_callback_entered{false};
std::atomic<bool> once_callback_release{false};

void once_callback() {
    once_calls.fetch_add(1, std::memory_order_relaxed);
    once_callback_entered.store(true, std::memory_order_release);
    while (!once_callback_release.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    once_visible.store(UINT32_C(0x5a5a), std::memory_order_release);
}

#ifndef EOS_RUST_TSAN_TEST
eos_rust_pthread_once_t recursive_once_control = EOS_RUST_PTHREAD_ONCE_INIT;
void once_noop() {}
void recursive_once_callback() {
    (void)eos_rust_pthread_once(&recursive_once_control, once_noop);
}
#endif

void test_once_and_exhaustion() {
    eos_rust_pthread_once_t control = EOS_RUST_PTHREAD_ONCE_INIT;
    once_calls.store(0);
    once_visible.store(0);
    once_callback_entered.store(false);
    once_callback_release.store(false);
    std::thread initializer([&] {
        expect(eos_rust_pthread_once(&control, once_callback) == 0,
               "pthread once initializer call failed");
    });
    while (!once_callback_entered.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    expect(control.words[0] != 0,
           "once record was not published before invoking the callback");

    std::atomic<uint32_t> contender_returns{0};
    std::vector<std::thread> callers;
    for (uint32_t index = 0; index != 31; ++index) {
        callers.emplace_back([&] {
            expect(eos_rust_pthread_once(&control, once_callback) == 0,
                   "pthread once call failed");
            expect(once_visible.load(std::memory_order_acquire) ==
                       UINT32_C(0x5a5a),
                   "once completion was published before callback writes");
            contender_returns.fetch_add(1, std::memory_order_release);
        });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    expect(contender_returns.load(std::memory_order_acquire) == 0,
           "once contender returned before callback completion");
    once_callback_release.store(true, std::memory_order_release);
    initializer.join();
    for (auto &caller : callers) caller.join();
    expect(once_calls.load() == 1 && contender_returns.load() == 31,
           "once callback did not run exactly once");
    expect(eos_rust_pthread_once(&control, once_callback) == 0 &&
               once_calls.load() == 1,
           "completed once ran again");
    expect(eos_sync_test_live_once() >= 1,
           "once records must remain for process lifetime");

#ifndef EOS_RUST_TSAN_TEST
    expect_blocks([] {
        (void)eos_rust_pthread_once(&recursive_once_control,
                                    recursive_once_callback);
    }, "recursive once on the same control must have normal deadlock semantics");
#endif

    eos_rust_pthread_mutex before_exhaustion{};
    expect(eos_rust_pthread_mutex_lock(&before_exhaustion) == 0 &&
               eos_rust_pthread_mutex_unlock(&before_exhaustion) == 0,
           "pre-exhaustion mutex allocation failed");
    eos_sync_test_exhaust_identities();
    eos_rust_pthread_mutex exhausted{};
    expect(eos_rust_pthread_mutex_lock(&exhausted) == 35 &&
               exhausted.words[0] == 0,
           "registry identity exhaustion must be deterministic and nonmutating");
    eos_rust_pthread_once_t exhausted_once = EOS_RUST_PTHREAD_ONCE_INIT;
    const uint32_t calls_before_exhaustion = once_calls.load();
    expect(eos_rust_pthread_once(&exhausted_once, once_callback) == 35 &&
               exhausted_once.words[0] == 0 &&
               once_calls.load() == calls_before_exhaustion,
           "once identity exhaustion must not publish or invoke the callback");
    expect(eos_rust_pthread_mutex_lock(&before_exhaustion) == 0 &&
               eos_rust_pthread_mutex_unlock(&before_exhaustion) == 0 &&
               eos_rust_pthread_mutex_destroy(&before_exhaustion) == 0,
           "identity exhaustion must not invalidate live records");
}

} // namespace

int main() {
    eos_host_test_reset();
    test_layouts_and_attributes();
    test_mutexes();
    test_concurrent_explicit_initialization();
    test_conditions_and_parker();
    test_rwlock_writer_preference();
    test_rwlock_faults_and_contention();
    test_once_and_exhaustion();
    expect(eos_sync_test_live_mutexes() == 0 &&
               eos_sync_test_live_conditions() == 0 &&
               eos_sync_test_live_rwlocks() == 0,
           "destroyable synchronization records leaked");
    return EXIT_SUCCESS;
}
