//@ compile-fail: attempted to instantiate an uninhabited type
#![feature(core_intrinsics)]

enum NoVariant {}

const CHECK: () = core::intrinsics::assert_inhabited::<(u16, ((), NoVariant))>();

fn main() { let _ = CHECK; }
