//@ compile-fail: Failed to find an impl
fn main() { let value = true; let _: bool = -value; }
