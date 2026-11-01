//@ compile-variant: -O
#![feature(explicit_tail_calls)]
use std::cell::Cell;

#[inline(never)]
fn finish(_first: bool, value: u16, counter: &Cell<u16>) -> u16 {
    value + counter.get()
}

#[inline(always)]
fn step(first: bool, value: u16, counter: &Cell<u16>) -> u16 {
    become finish(first, value, counter)
}

#[inline(never)]
fn relay(first: bool, value: u16, counter: &Cell<u16>) -> u16 {
    if first {
        counter.set(11);
        step(first, value, counter)
    } else {
        counter.set(17);
        step(first, value, counter)
    }
}

fn main() {
    let counter = Cell::new(0);
    assert_eq!(relay(true, 3, &counter), 14);
    assert_eq!(counter.get(), 11);
    assert_eq!(relay(false, 5, &counter), 22);
    assert_eq!(counter.get(), 17);
}
