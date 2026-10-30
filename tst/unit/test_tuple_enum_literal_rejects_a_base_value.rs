//@ compile-fail: Enum literals cannot have a base value
enum Choice { Pair(u8, u8) }
fn main() { let base = Choice::Pair(1, 2); let _ = Choice::Pair { 0: 3, ..base }; }
