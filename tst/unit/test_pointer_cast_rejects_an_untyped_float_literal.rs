//@ compile-fail: Invalid cast to pointer from floating point literal
fn main() { let _ = 1.5 as *const u8; }
