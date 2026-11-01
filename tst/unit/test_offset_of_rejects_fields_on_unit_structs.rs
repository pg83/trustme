//@ compile-fail: no named field
struct Empty;
fn main() { let _ = std::mem::offset_of!(Empty, absent); }
