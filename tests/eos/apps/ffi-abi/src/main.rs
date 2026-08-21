#![feature(restricted_std)]

use std::hint::black_box;

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Small {
    Zero = 0,
    One = 1,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
struct Mixed {
    integer: i32,
    single: f32,
    double: f64,
}

#[unsafe(no_mangle)]
extern "C" fn eos_ffi_roundtrip(
    integer: i32,
    small: Small,
    single: f32,
    double: f64,
    mixed: Mixed,
) -> Mixed {
    Mixed {
        integer: integer + mixed.integer + small as i32,
        single: single + mixed.single,
        double: double + mixed.double,
    }
}

fn main() {
    let call: extern "C" fn(i32, Small, f32, f64, Mixed) -> Mixed = eos_ffi_roundtrip;
    let result = call(
        black_box(7),
        black_box(Small::One),
        black_box(1.25),
        black_box(2.5),
        black_box(Mixed {
            integer: 11,
            single: 3.75,
            double: 4.5,
        }),
    );
    assert_eq!(result.integer, 19);
    assert_eq!(result.single, 5.0);
    assert_eq!(result.double, 7.0);
    black_box(Small::Zero);
}
