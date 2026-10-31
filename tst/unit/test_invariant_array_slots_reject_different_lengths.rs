//@ compile-fail: Type mismatch
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn main() { let mut slot = [0u8; 2]; replace(&mut slot, [1u8; 3]); }
