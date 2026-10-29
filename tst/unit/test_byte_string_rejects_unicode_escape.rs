//@ compile-fail: Unicode escapes are not allowed in byte literals
const BAD: &[u8] = b"a\
    \u{100}";
fn main() { let _ = BAD; }
