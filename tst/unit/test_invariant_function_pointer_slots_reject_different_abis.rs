//@ compile-fail: Type mismatch
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn rust_step() {}
extern "C" fn c_step() {}
fn main() { let mut slot: fn() = rust_step; replace(&mut slot, c_step as extern "C" fn()); }
