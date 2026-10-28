//@ compile-fail: assert_inhabited ! failed
#![feature(core_intrinsics, never_type)]
const _: () = core::intrinsics::assert_inhabited::<!>();
fn main() {}
