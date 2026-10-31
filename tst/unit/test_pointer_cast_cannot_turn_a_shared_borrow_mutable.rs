//@ compile-fail: Invalid cast from
fn main() { let value = 37u32; let _ = &value as *mut u32; }
