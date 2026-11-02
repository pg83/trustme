//@ compile-fail: type
mod storage {}
fn consume(_: *const storage<{ 6 + 7 }>) {}
fn main() {}
