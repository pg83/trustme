//@ compile-variant: -O
#![feature(explicit_tail_calls)]

#[inline(never)]
fn finish(value: u16) -> u16 { value * 3 }
#[inline(always)]
fn step(value: u16) -> u16 { become finish(value) }
#[inline(never)]
fn outer(value: u16) -> u16 {
    let result = step(value);
    result + 5
}

fn main() {
    assert_eq!(outer(7), 26);
    assert_eq!(outer(11), 38);
}
