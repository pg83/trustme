//@ compile-fail: Duplicate index
struct Pair(u8, u8);
fn main() { let _ = Pair { 0: 1, 0: 2 }; }
