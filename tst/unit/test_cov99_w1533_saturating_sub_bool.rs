//@ compile-fail: saturating_sub - bool
#![feature(core_intrinsics)]

fn main() {
    let value = core::intrinsics::saturating_sub::<bool>(true, false);
    assert!(value);
}
