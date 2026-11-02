//@ compile-fail: cast
fn address() {}
fn main() { let _ = address as *const [u8]; }
