//@ compile-fail: attempted to instantiate an uninhabited type
#![feature(core_intrinsics)]

enum Void {}
struct Record<T> { prefix: u16, payload: T }
struct Envelope<T>(u8, Record<T>);

const CHECK: () = core::intrinsics::assert_inhabited::<Envelope<Void>>();

fn main() { let _ = CHECK; }
