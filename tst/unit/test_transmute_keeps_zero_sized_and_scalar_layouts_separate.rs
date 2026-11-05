//@ compile-variant: -O
#![feature(core_intrinsics)]
#![allow(internal_features)]

#[repr(align(32))]
struct Empty;

#[inline(never)]
fn to_empty_array(value: Empty) -> [u16; 0] {
    unsafe { std::mem::transmute(value) }
}

#[inline(never)]
fn to_unit(value: [u64; 0]) {
    unsafe { std::mem::transmute(value) }
}

#[no_mangle]
pub unsafe fn unchecked_zero_to_nonzero(value: ()) -> u16 {
    core::intrinsics::transmute_unchecked(value)
}

#[inline(never)]
fn scalar_bits(value: u32) -> f32 {
    unsafe { std::mem::transmute(value) }
}

fn main() {
    assert_eq!(to_empty_array(Empty), []);
    to_unit([]);
    assert_eq!(scalar_bits(std::hint::black_box(0x3fc00000)), 1.5);
}
