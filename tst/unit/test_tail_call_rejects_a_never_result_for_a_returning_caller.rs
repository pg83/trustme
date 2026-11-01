//@ compile-fail: mismatched signatures in tail call
#![feature(explicit_tail_calls)]
fn target(_: u16) -> ! { loop {} }
fn relay(value: u16) -> u16 { become target(value) }
fn main() { let _ = relay; }
