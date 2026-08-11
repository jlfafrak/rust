#include <stdint.h>

static _Thread_local int32_t eos_host_errno;

static int32_t *eos_port_errno_location(void) {
    return &eos_host_errno;
}
