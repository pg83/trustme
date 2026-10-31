//@ compile-fail: invalid left-hand side of assignment
fn main() { let function: fn() -> u8 = || 17; (function)() = 23; }
