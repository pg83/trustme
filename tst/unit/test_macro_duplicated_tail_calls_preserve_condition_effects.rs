#![feature(explicit_tail_calls)]
use std::cell::Cell;

macro_rules! repeated_tail {
    ($condition:expr, $tail:expr) => { if $condition { $tail } else { $tail } };
}

#[inline(never)]
fn finish(first: bool, value: u16, counter: &Cell<u16>) -> u16 {
    value + counter.get() + if first { 3 } else { 5 }
}

#[inline(never)]
fn relay(first: bool, value: u16, counter: &Cell<u16>) -> u16 {
    repeated_tail!({ counter.set(counter.get() + 1); first }, { become finish(first, value, counter) })
}

fn main() {
    let counter = Cell::new(0);
    assert_eq!(relay(true, 7, &counter), 11);
    assert_eq!(relay(false, 7, &counter), 14);
    assert_eq!(counter.get(), 2);
}
