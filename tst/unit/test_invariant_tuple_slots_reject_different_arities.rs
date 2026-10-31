//@ compile-fail: Type mismatch
fn replace<T>(slot: &mut T, value: T) { *slot = value; }
fn main() { let mut slot = (1u8, 2u8); replace(&mut slot, (3u8, 4u8, 5u8)); }
