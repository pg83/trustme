//@ compile-fail: mismatched signatures in tail call
#![feature(explicit_tail_calls)]
fn target(value: u32) -> u16 { value as u16 }
fn relay(value: u16) -> u16 { become target(value as u32) }
fn main() { let _ = relay(17); }
