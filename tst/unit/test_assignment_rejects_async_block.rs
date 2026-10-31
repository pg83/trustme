//@ compile-fail: invalid left-hand side of assignment
fn main() { (async { 1u8 }) = async { 2u8 }; }
