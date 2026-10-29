//@ compile-fail: non-ASCII character in byte literal
const BAD: &[u8] = br##"part"#λ"##;
fn main() { let _ = BAD; }
