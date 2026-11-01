//@ compile-fail: mismatched signatures in tail call
#![feature(explicit_tail_calls)]
fn target(value: u16) -> u16 { value + 1 }
fn relay(_: u16, value: u16) -> u16 { become target(value) }
fn main() { let _ = relay(3, 5); }
