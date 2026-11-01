//@ compile-fail: No macro arm matched
#![feature(explicit_tail_calls)]
macro_rules! expression { ($value:expr) => { () }; }
fn main() { expression!(become target(4)); }
