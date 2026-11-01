//@ compile-fail: field index
fn main() { let _ = std::mem::offset_of!((u8, u16), 7); }
