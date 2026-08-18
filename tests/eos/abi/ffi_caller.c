#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t eos_ffi_containment_probe(int32_t should_panic);

#ifdef __cplusplus
}
#endif

int main(void) {
    if (eos_ffi_containment_probe(INT32_C(0)) != INT32_C(0)) {
        return 1;
    }
    if (eos_ffi_containment_probe(INT32_C(1)) != INT32_C(-1)) {
        return 2;
    }
    return 0;
}
