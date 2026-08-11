#include "eos_rust_abi.h"

#include "eos_port.h"

#include <errno.h>

#ifndef EOS_RUST_PORT_SOURCE
#error "EOS_RUST_PORT_SOURCE must name exactly one EOS native port"
#endif

#include EOS_RUST_PORT_SOURCE

/*
 * Keep private service implementations in this translation unit so private
 * helpers have local linkage and cannot expand the static archive's ABI.
 */
#include "eos_error.c"

uint32_t eos_rust_abi_version(void) {
    return (EOS_RUST_ABI_MAJOR << 16) | EOS_RUST_ABI_MINOR;
}

int32_t eos_rust_abi_require(uint32_t major, uint32_t minimum_minor) {
    if (major != EOS_RUST_ABI_MAJOR) {
        *eos_port_errno_location() = EPROTONOSUPPORT;
        return -1;
    }
    if (minimum_minor > EOS_RUST_ABI_MINOR) {
        *eos_port_errno_location() = ENOTSUP;
        return -1;
    }
    return 0;
}

int32_t *eos_rust_errno_location(void) {
    return eos_port_errno_location();
}
