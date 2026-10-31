//@ compile-fail: floating
#![feature(core_intrinsics, float_algebraic)]
#![allow(internal_features)]
fn main() {
    let _ = core::intrinsics::fadd_algebraic(true, false);
}
