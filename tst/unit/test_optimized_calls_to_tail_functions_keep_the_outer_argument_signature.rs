//@ compile-variant: -O
#![feature(explicit_tail_calls)]

#[inline(never)]
fn finish(value: u16) -> u16 { value + 3 }
#[inline(always)]
fn step(value: u16) -> u16 { become finish(value) }
#[inline(never)]
fn outer(value: u32) -> u16 { step(value as u16) + 7 }

fn main() {
    assert_eq!(outer(11), 21);
    assert_eq!(outer(65541), 15);
}
