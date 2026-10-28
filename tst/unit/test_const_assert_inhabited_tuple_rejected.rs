//@ compile-fail: assert_inhabited
#![feature(core_intrinsics)]
enum Empty {}
const _: () = core::intrinsics::assert_inhabited::<(u8, ((), Empty))>();
fn main() {}
