//@ compile-fail: attempted to instantiate an uninhabited type
#![feature(core_intrinsics)]

enum Void {}
enum Alternatives<T> { Single(T), Multiple { prefix: u8, values: [T; 2] } }

const CHECK: () = core::intrinsics::assert_inhabited::<Alternatives<Void>>();

fn main() { let _ = CHECK; }
