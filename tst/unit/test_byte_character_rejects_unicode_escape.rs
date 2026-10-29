//@ compile-fail: Unicode escapes are not allowed in byte literals
const BAD: u8 = b'\u{41}';
fn main() { let _ = BAD; }
