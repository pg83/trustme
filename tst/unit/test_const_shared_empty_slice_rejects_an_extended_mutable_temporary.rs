//@ compile-fail: A mutable reference is not allowed in the final value of a constant
const EMPTY: &[u8] = &mut [];
fn main() { assert_eq!(EMPTY.len(), 0); }
