//@ compile-fail: assert_inhabited
#![feature(core_intrinsics)]
enum Empty {}
struct Named<T> { first: u8, value: T }
struct Pair<T>(u8, T);
const _: () = core::intrinsics::assert_inhabited::<Pair<Named<Empty>>>();
fn main() {}
