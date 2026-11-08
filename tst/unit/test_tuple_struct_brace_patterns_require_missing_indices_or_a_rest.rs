//@ compile-fail: Missing index
struct Pair(u8, u8);
fn main() { let Pair { 1: second } = Pair(3, 5); }
