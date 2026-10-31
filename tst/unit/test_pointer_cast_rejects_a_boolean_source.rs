//@ compile-fail: Invalid cast to pointer
fn main() { let _ = true as *const u8; }
