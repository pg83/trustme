//@ compile-fail: `become` is not allowed in closures
#![feature(explicit_tail_calls)]
fn target(value: u16) -> u16 { value + 1 }
fn main() {
    let relay = |value: u16| -> u16 { become target(value) };
    let _ = relay(23);
}
