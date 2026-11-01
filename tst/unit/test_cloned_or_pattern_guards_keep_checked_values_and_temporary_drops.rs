//@ compile-flags: -Coverflow-checks=on
//@ compile-variant: -O -Coverflow-checks=on
use std::cell::Cell;

struct Guard<'a>(&'a Cell<u16>);
impl Drop for Guard<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 3); } }
#[inline(never)]
fn select(pair: (u16, u16), events: &Cell<u16>) -> u16 {
    match pair {
        (0, value) | (value, 0) if {
            let temporary = Guard(events);
            let checked = value + 1;
            drop(temporary);
            checked == 8
        } => value,
        _ => 11,
    }
}
fn main() {
    let events = Cell::new(0);
    assert_eq!(select((0, 7), &events), 7);
    assert_eq!(events.get(), 3);
    assert_eq!(select((7, 0), &events), 7);
    assert_eq!(events.get(), 6);
    assert_eq!(select((0, 5), &events), 11);
    assert_eq!(events.get(), 9);
}
