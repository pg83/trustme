//@ compile-fail: cannot match
#![feature(box_patterns)]
struct Token;
fn main() { let box _ = Token; }
