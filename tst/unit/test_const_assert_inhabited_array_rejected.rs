//@ compile-fail: assert_inhabited
#![feature(core_intrinsics)]
enum Empty {}
const fn check<const N: usize>() {
    core::intrinsics::assert_inhabited::<[Empty; N]>();
}
const _: () = check::<1>();
fn main() {}
