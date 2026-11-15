//@ crate-type: lib
//@ compile-fail: saturating_add
//@ compile-flags: -C opt-level=0
#![feature(core_intrinsics)]

#[no_mangle]
pub fn cov99_w1532_saturating_add_float(a: f32, b: f32) -> f32 {
    core::intrinsics::saturating_add(a, b)
}
