//@ compile-fail: assert_inhabited
#![feature(core_intrinsics, never_type)]
enum Empty {}
enum AllEmpty { A(Empty), B { value: (!,) } }
const _: () = core::intrinsics::assert_inhabited::<AllEmpty>();
fn main() {}
