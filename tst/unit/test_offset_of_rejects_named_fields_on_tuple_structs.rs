//@ compile-fail: no named field
struct Pair(u8, u16);
fn main() { let _ = std::mem::offset_of!(Pair, absent); }
