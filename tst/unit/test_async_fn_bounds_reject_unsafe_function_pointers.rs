//@ compile-fail: error:
fn require<F: std::ops::AsyncFnOnce(u32) -> u32>(_: F) {}
unsafe fn action(value: u32) -> std::future::Ready<u32> { std::future::ready(value + 131) }
fn main() { require(action as unsafe fn(u32) -> std::future::Ready<u32>); }
