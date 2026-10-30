//@ compile-flags: -Zunleash-the-miri-inside-of-you -Zmir-opt-level=0
#![feature(explicit_tail_calls)]
#![allow(incomplete_features)]
const fn increment(_: bool, value: u16) -> u16 { value + 7 }
const fn double(_: bool, value: u16) -> u16 { value * 2 }
const fn relay(first: bool, value: u16) -> u16 {
    let next: fn(bool, u16) -> u16 = if first { increment } else { double };
    become next(first, value)
}
const FIRST: u16 = relay(true, 19);
const SECOND: u16 = relay(false, 23);
fn main() { assert_eq!((FIRST, SECOND), (26, 46)); }
