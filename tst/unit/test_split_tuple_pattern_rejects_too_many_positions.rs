//@ compile-fail: Split-tuple pattern with an incorrect number of fields
fn main() { let (a, b, .., c) = (3u8, 5u8); let _ = (a, b, c); }
