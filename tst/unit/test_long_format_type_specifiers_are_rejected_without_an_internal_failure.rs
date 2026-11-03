//@ compile-fail: Unknown formatting type specifier
fn main() { let _ = format!("{:word}", 127u32); }
