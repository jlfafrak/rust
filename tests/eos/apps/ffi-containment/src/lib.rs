#![feature(restricted_std)]
#![cfg_attr(target_os = "eos", deny(ffi_unwind_calls))]

use std::cell::Cell;
use std::ffi::c_int;
use std::panic::{AssertUnwindSafe, catch_unwind};

/// Fixture-local stable result for a panic contained at the ordinary C ABI.
const EOS_FFI_CONTAINED_PANIC: c_int = -1;

struct DropGuard<'a>(&'a Cell<bool>);

impl Drop for DropGuard<'_> {
    fn drop(&mut self) {
        self.0.set(true);
    }
}

/// Ordinary-C entry point used by EOS, C, and C++ callers.
///
/// Returns zero for normal work and `EOS_FFI_CONTAINED_PANIC` after catching a
/// Rust panic. No panic is permitted to cross this function's C ABI boundary.
#[unsafe(no_mangle)]
pub extern "C" fn eos_ffi_containment_probe(should_panic: c_int) -> c_int {
    let dropped = Cell::new(false);
    let result = catch_unwind(AssertUnwindSafe(|| {
        let _guard = DropGuard(&dropped);
        if should_panic != 0 {
            panic!("contained EOS FFI panic");
        }
    }));

    match result {
        Ok(()) if dropped.get() => 0,
        Err(_) if dropped.get() => EOS_FFI_CONTAINED_PANIC,
        _ => std::process::abort(),
    }
}
