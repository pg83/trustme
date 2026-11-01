//@ compile-fail: pattern does not mention all fields
struct Pair(u8, u16);
fn main() { let Pair {} = Pair(3, 5); }
