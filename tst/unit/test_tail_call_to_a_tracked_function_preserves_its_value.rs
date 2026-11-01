//@ compile-variant: -O
#![feature(explicit_tail_calls)]

#[track_caller]
#[inline(never)]
fn finish(value: u16) -> u16 {
    if value == 7 { return 10; }
    value + 3
}
#[inline(never)]
fn relay(value: u16) -> u16 { become finish(value) }

fn main() {
    assert_eq!(relay(7), 10);
    assert_eq!(relay(11), 14);
}
