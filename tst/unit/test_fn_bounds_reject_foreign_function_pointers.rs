//@ compile-fail: error:
fn require<F: Fn(u32) -> u32>(_: F) {}
extern "C" fn action(value: u32) -> u32 { value + 113 }
fn main() { require(action as extern "C" fn(u32) -> u32); }
