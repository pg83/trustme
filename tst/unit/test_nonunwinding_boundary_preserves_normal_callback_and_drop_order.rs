//@ compile-flags: -Cpanic=unwind
//@ compile-variant: -O
use std::cell::Cell;

struct Mark<'a>(&'a Cell<u64>, u64);
impl Drop for Mark<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}

#[inline(never)]
extern "C" fn observe(events: &Cell<u64>, callback: fn(&Cell<u64>)) {
    let _first = Mark(events, 3);
    let _second = Mark(events, 5);
    callback(events);
}

fn callback(events: &Cell<u64>) { events.set(2); }
fn main() {
    let events = Cell::new(0);
    observe(&events, std::hint::black_box(callback));
    assert_eq!(events.get(), 253);
}
