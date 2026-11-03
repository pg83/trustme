//@ compile-fail: error:
fn require<F: Fn(u32) -> u32>(_: F) {}
fn main() { require(|| 149u32); }
