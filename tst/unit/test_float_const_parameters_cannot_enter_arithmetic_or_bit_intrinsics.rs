//@ compile-fail: error
#![feature(core_intrinsics)]
const fn shifted<const VALUE: f64>() -> f64 { VALUE + 0.5 }
const fn absolute<const VALUE: f64>() -> f64 {
    unsafe { core::intrinsics::fabsf64(VALUE) }
}
const SHIFTED: f64 = shifted::<{ 1.25 }>();
const ABSOLUTE: f64 = absolute::<{ -2.5 }>();
fn main() {
    assert_eq!(SHIFTED, 1.75);
    assert_eq!(ABSOLUTE, 2.5);
}
