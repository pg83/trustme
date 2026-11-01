//@ compile-fail: mismatched signatures in tail call
#![feature(explicit_tail_calls)]
fn target(_: u16) -> &'static [u8; 2] { b"ok" }
fn relay(value: u16) -> &'static [u8] { become target(value) }
fn main() { let _ = relay(19); }
