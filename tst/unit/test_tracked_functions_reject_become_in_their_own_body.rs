//@ compile-fail: a function marked with `#[track_caller]` cannot perform a tail-call
#![feature(explicit_tail_calls)]
fn finish(value: u16) -> u16 { value + 3 }
#[track_caller]
fn relay(value: u16) -> u16 { become finish(value) }
fn main() { let _ = relay(7); }
