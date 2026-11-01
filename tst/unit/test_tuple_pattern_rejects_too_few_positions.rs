//@ compile-fail: Tuple pattern with an incorrect number of fields
fn main() { let (a, b) = (3u8, 5u8, 7u8); let _ = (a, b); }
