//@ compile-fail: no named field
struct Record { value: u16 }
fn main() { let _ = std::mem::offset_of!(Record, absent); }
