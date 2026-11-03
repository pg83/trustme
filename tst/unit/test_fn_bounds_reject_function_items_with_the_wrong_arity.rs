//@ compile-fail: error:
fn require<F: Fn(u32) -> u32>(_: F) {}
fn no_args() -> u32 { 103 }
fn main() { require(no_args); }
