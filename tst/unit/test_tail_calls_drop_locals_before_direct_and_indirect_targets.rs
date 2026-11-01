#![feature(explicit_tail_calls)]
use std::cell::Cell;
struct Watch<'a>(&'a Cell<u16>);
impl Drop for Watch<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
fn argument(counter: &Cell<u16>, value: u16) -> u16 {
    assert_eq!(counter.get(), 0);
    value
}
fn target(counter: &Cell<u16>, value: u16) -> u16 {
    assert_eq!(counter.get(), 1);
    value + 3
}
#[inline(never)]
fn direct(counter: &Cell<u16>, value: u16) -> u16 {
    let _watch = Watch(counter);
    become target(counter, argument(counter, value))
}
#[inline(never)]
fn indirect(counter: &Cell<u16>, value: u16) -> u16 {
    let _watch = Watch(counter);
    let next: fn(&Cell<u16>, u16) -> u16 = target;
    become next(counter, argument(counter, value))
}
fn main() {
    let counter = Cell::new(0);
    assert_eq!(direct(&counter, 53), 56);
    assert_eq!(counter.get(), 1);
    counter.set(0);
    assert_eq!(indirect(&counter, 59), 62);
    assert_eq!(counter.get(), 1);
}
