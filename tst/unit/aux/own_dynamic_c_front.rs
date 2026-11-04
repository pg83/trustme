#![no_std]
#[link(name = "own_dynamic_value", kind = "dylib")]
extern "C" { fn own_dynamic_shift(value: u32) -> u32; }
pub fn shift(value: u32) -> u32 { unsafe { own_dynamic_shift(value) } }
