//@ compile-fail: rotate_right - bool
#![feature(core_intrinsics)]

fn main() {
    let value = std::hint::black_box(true);
    assert!(core::intrinsics::rotate_right::<bool>(value, 1));
}
