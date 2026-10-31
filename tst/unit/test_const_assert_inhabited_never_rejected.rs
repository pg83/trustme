//@ compile-fail: attempted to instantiate an uninhabited type: !
#![feature(core_intrinsics, never_type)]
const _: () = core::intrinsics::assert_inhabited::<!>();
fn main() {}
