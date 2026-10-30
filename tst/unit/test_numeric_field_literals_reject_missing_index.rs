//@ compile-fail: Missing index
struct Pair(u8, u8);
fn main() { let _ = Pair { 1: 2 }; }
