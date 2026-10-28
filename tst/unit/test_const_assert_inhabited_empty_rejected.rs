//@ compile-fail: assert_inhabited
#![feature(core_intrinsics)]
enum Empty {}
const _: () = core::intrinsics::assert_inhabited::<Empty>();
fn main() {}
