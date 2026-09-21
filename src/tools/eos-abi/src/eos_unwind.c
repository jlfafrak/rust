#include <stdint.h>

/*
 * libgcc's ARM EHABI implementation calls this optional hook before falling
 * back to platform-wide unwind metadata.  MARTOS applications own their
 * unwind index, so returning this image's retained range is both sufficient
 * and independent of the current program counter.
 */
extern const uint32_t __exidx_start[];
extern const uint32_t __exidx_end[];

uintptr_t __gnu_Unwind_Find_exidx(uintptr_t program_counter, int *count) {
    (void)program_counter;

    if (count != 0) {
        *count = (int)((__exidx_end - __exidx_start) / 2U);
    }
    return (uintptr_t)__exidx_start;
}
