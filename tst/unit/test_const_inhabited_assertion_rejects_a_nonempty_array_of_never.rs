//@ compile-fail: attempted to instantiate an uninhabited type
#![feature(core_intrinsics, never_type)]

const CHECK: () = core::intrinsics::assert_inhabited::<[!; 2]>();

fn main() { let _ = CHECK; }
