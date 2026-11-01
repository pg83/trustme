#![feature(explicit_tail_calls)]
use std::cell::Cell;
struct Watch<'a>(&'a Cell<u16>);
impl Drop for Watch<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
fn argument(counter: &Cell<u16>, value: u16) -> u16 {
    assert_eq!(counter.get(), 0);
    value
}
fn plus(counter: &Cell<u16>, value: u16) -> u16 {
    assert_eq!(counter.get(), 1);
    value + 7
}
fn times(counter: &Cell<u16>, value: u16) -> u16 {
    assert_eq!(counter.get(), 1);
    value * 3
}
#[inline(never)]
fn relay(counter: &Cell<u16>, value: u16) -> u16 {
    let _watch = Watch(counter);
    if value & 1 == 0 { become plus(counter, argument(counter, value)) }
    else { become times(counter, argument(counter, value)) }
}
fn main() {
    let counter = Cell::new(0);
    assert_eq!(relay(&counter, 60), 67);
    counter.set(0);
    assert_eq!(relay(&counter, 61), 183);
    assert_eq!(counter.get(), 1);
}
