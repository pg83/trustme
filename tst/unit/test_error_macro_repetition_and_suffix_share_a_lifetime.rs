//@ compile-fail: ambiguity
macro_rules! choose { ($($prefix:lifetime)* $last:lifetime) => { 7u16 }; }
fn main() { let _ = choose!('item); }
