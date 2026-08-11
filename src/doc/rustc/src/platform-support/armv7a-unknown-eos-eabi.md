# `armv7a-unknown-eos-eabi`

**Tier: 3**

This target supports EOS applications on the ARM processing system in the
Xilinx/AMD Zynq-7000 XC7Z030 and XC7Z045 device families. It is maintained in
the EOS Rust fork; its availability and support are not promises made by the
upstream Rust project.

The target generates A32 code for the Armv7-A Cortex-A9 and uses the EOS
softfp procedure-call ABI. It enables the processors' VFPv3-D32 and NEON
hardware for code generation, but floating-point arguments and return values
still follow the soft-float calling convention. There is no hard-float EOS
target.

Native ELF thread-local storage is disabled because the EOS loader does not
yet have a tested `PT_TLS` and ARM TLS relocation contract. The EOS standard
library instead supplies its runtime TLS through the EOS compatibility layer.

## Requirements

Programs must be linked with `eos-rust-link` from the matching EOS Rust SDK.
The linker wrapper selects the pinned ARM GNU toolchain, EOS SDK libraries,
and EOS application linker script; a generic system linker is not sufficient.

The Rust project does not build or test this Tier 3 target automatically.
Each EOS Rust SDK release is tested manually on both XC7Z030 and XC7Z045
boards, including debug and release applications.
