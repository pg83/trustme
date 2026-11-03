//@ compile-fail: error:
fn require<F: Fn(u32) -> u32>(_: F) {}
unsafe fn action(value: u32) -> u32 { value + 109 }
fn main() { require(action as unsafe fn(u32) -> u32); }
