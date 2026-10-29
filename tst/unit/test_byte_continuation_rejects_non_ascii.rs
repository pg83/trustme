//@ compile-fail: non-ASCII character in byte literal
const BAD: &[u8] = b"a\
    é";
fn main() { let _ = BAD; }
