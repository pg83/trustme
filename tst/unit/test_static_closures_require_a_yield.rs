//@ compile-fail: Invalid use of `static` on non-yielding closure
#![feature(coroutines)]
fn main() { let _ = static || {}; }
