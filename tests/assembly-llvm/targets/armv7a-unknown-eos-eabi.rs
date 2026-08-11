//@ add-minicore
//@ assembly-output: emit-asm
//@ compile-flags: --target armv7a-unknown-eos-eabi -Copt-level=3
//@ needs-llvm-components: arm

#![feature(intrinsics, lang_items, no_core, repr_simd)]
#![no_core]
#![crate_type = "lib"]

extern crate minicore;

// The EOS AAPCS softfp ABI passes lhs/rhs in r0/r1 and the result in r0 while still allowing VFP
// instructions for the calculation.

// CHECK-LABEL: eos_softfp_add:
// CHECK-DAG: vmov [[LHS:s[0-9]+]], r0
// CHECK-DAG: vmov [[RHS:s[0-9]+]], r1
// CHECK: vadd.f32 [[SUM:s[0-9]+]], {{s[0-9]+}}, {{s[0-9]+}}
// CHECK: vmov r0, [[SUM]]
#[no_mangle]
pub extern "C" fn eos_softfp_add(lhs: f32, rhs: f32) -> f32 {
    unsafe { fadd_fast(lhs, rhs) }
}

#[repr(simd)]
pub struct I32x4([i32; 4]);

#[rustc_intrinsic]
unsafe fn simd_add<T>(lhs: T, rhs: T) -> T;

#[rustc_intrinsic]
unsafe fn fadd_fast<T>(lhs: T, rhs: T) -> T;

// The EOS Cortex-A9 feature set must lower a four-lane vector add to a 128-bit NEON instruction.

// CHECK-LABEL: eos_neon_add:
// CHECK: vadd.i32 q{{[0-9]+}}, q{{[0-9]+}}, q{{[0-9]+}}
#[no_mangle]
pub unsafe fn eos_neon_add(lhs: I32x4, rhs: I32x4) -> I32x4 {
    simd_add(lhs, rhs)
}
