//@ compile-fail: cast
fn main() { let value = 71u8; let _ = (&value as *const u8) as f64; }
