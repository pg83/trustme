//@ compile-fail: cannot match
fn main() { let (a, b) = 13u32; let _ = (a, b); }
