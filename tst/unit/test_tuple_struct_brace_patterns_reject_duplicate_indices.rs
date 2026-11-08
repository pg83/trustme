//@ compile-fail: Duplicate index
struct Pair(u8, u8);
fn main() { let Pair { 0: first, 0: second } = Pair(3, 5); }
