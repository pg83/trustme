//@ compile-fail: error:
fn require<F: std::ops::AsyncFnOnce(u32) -> u32>(_: F) {}
fn action(value: u32) -> u32 { value + 137 }
fn main() { require(action); }
