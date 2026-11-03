//@ compile-fail: error:
fn require<F: Fn(u32) -> u32>(_: F) {}
unsafe fn action(value: u32) -> u32 { value + 107 }
fn main() { require(action); }
