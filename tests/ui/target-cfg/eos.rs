//@ add-minicore
//@ check-pass
//@ compile-flags: --target armv7a-unknown-eos-eabi
//@ needs-llvm-components: arm

#![feature(no_core)]
#![no_core]
#![crate_type = "lib"]

extern crate minicore;
use minicore::*;

#[cfg(not(all(
    target_arch = "arm",
    target_os = "eos",
    target_family = "unix",
    target_abi = "eabi",
    target_endian = "little",
    target_pointer_width = "32"
)))]
compile_error!("incorrect EOS target cfg");
fn main() {}
