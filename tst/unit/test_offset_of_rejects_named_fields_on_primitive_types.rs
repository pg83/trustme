//@ compile-fail: no named field
fn main() { let _ = std::mem::offset_of!(u32, absent); }
