//@ compile-variant: -O
#![feature(explicit_tail_calls)]
use std::cell::Cell;

struct Guard<'a>(&'a Cell<u16>);
impl Drop for Guard<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
#[inline(never)]
fn finish(value: u16, _counter: &Cell<u16>) -> u16 { value + 3 }
#[inline(always)]
fn step(value: u16, counter: &Cell<u16>) -> u16 { become finish(value, counter) }
#[inline(never)]
fn outer(value: u16, counter: &Cell<u16>) -> u16 {
    let guard = Guard(counter);
    let result = step(value, counter);
    assert_eq!(counter.get(), 0);
    drop(guard);
    result + 11
}

fn main() {
    let counter = Cell::new(0);
    assert_eq!(outer(7, &counter), 21);
    assert_eq!(counter.get(), 1);
}
