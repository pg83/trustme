//@ compile-variant: -O
const EMPTY: &[u8] = &[];
fn main() { assert_eq!(EMPTY.len(), 0); }
