#include <stdint.h>
#include <arm_neon.h>

typedef enum eos_small {
    EOS_SMALL_ZERO = 0,
    EOS_SMALL_ONE = 1,
    EOS_SMALL_TWO = 2,
} eos_small;

typedef struct eos_mixed {
    int32_t integer;
    float single;
    double double_value;
} eos_mixed;

extern int32_t eos_rust_accept(
    int32_t integer,
    eos_small small,
    float single,
    double double_value,
    eos_mixed mixed);
extern eos_mixed eos_rust_return(
    int32_t integer,
    eos_small small,
    float single,
    double double_value,
    eos_mixed mixed);

__attribute__((noinline, used))
int32_t eos_c_accept(
    int32_t integer,
    eos_small small,
    float single,
    double double_value,
    eos_mixed mixed) {
    return integer + (int32_t)small + (int32_t)(single + mixed.single) +
           (int32_t)(double_value + mixed.double_value) + mixed.integer;
}

__attribute__((noinline, used))
eos_mixed eos_c_return(
    int32_t integer,
    eos_small small,
    float single,
    double double_value,
    eos_mixed mixed) {
    eos_mixed result = {
        integer + mixed.integer + (int32_t)small,
        single + mixed.single,
        double_value + mixed.double_value,
    };
    return result;
}

__attribute__((noinline, used))
void eos_c_neon_add(
    const float *left,
    const float *right,
    float *result) {
    vst1q_f32(result, vaddq_f32(vld1q_f32(left), vld1q_f32(right)));
}

__attribute__((noinline, used))
int32_t eos_c_calls_rust(
    int32_t integer,
    eos_small small,
    float single,
    double double_value,
    eos_mixed mixed) {
    eos_mixed returned =
        eos_rust_return(integer, small, single, double_value, mixed);
    return eos_rust_accept(integer, small, single, double_value, returned);
}
