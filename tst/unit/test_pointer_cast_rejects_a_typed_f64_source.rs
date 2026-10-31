//@ compile-fail: Invalid cast to pointer
fn main() { let _ = 1.5f64 as *const u8; }
