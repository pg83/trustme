//@ compile-fail: Failed to find an impl
fn main() { let value = 3.5f64; let _: f64 = !value; }
