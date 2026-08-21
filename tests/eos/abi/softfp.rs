#![no_std]

#[repr(C)]
#[derive(Clone, Copy)]
pub enum EosSmall {
    Zero = 0,
    One = 1,
    Two = 2,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct EosMixed {
    pub integer: i32,
    pub single: f32,
    pub double_value: f64,
}

unsafe extern "C" {
    fn eos_c_accept(
        integer: i32,
        small: EosSmall,
        single: f32,
        double_value: f64,
        mixed: EosMixed,
    ) -> i32;
    fn eos_c_return(
        integer: i32,
        small: EosSmall,
        single: f32,
        double_value: f64,
        mixed: EosMixed,
    ) -> EosMixed;
}

#[unsafe(no_mangle)]
#[inline(never)]
pub extern "C" fn eos_rust_accept(
    integer: i32,
    small: EosSmall,
    single: f32,
    double_value: f64,
    mixed: EosMixed,
) -> i32 {
    integer
        + small as i32
        + (single + mixed.single) as i32
        + (double_value + mixed.double_value) as i32
        + mixed.integer
}

#[unsafe(no_mangle)]
#[inline(never)]
pub extern "C" fn eos_rust_return(
    integer: i32,
    small: EosSmall,
    single: f32,
    double_value: f64,
    mixed: EosMixed,
) -> EosMixed {
    EosMixed {
        integer: integer + mixed.integer + small as i32,
        single: single + mixed.single,
        double_value: double_value + mixed.double_value,
    }
}

#[unsafe(no_mangle)]
#[inline(never)]
pub extern "C" fn eos_rust_calls_c(
    integer: i32,
    small: EosSmall,
    single: f32,
    double_value: f64,
    mixed: EosMixed,
) -> i32 {
    let returned = unsafe { eos_c_return(integer, small, single, double_value, mixed) };
    unsafe { eos_c_accept(integer, small, single, double_value, returned) }
}
