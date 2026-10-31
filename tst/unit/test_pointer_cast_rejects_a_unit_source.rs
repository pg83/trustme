//@ compile-fail: Invalid cast to pointer
fn main() { let _ = () as *const u8; }
