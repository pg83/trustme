//@ compile-fail: cast
fn main() { let value = 31u16; let _ = &value as usize; }
