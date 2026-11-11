//@ compile-flags: -Cpanic=unwind -Zmir-opt-level=2 -O
use std::cell::Cell;

type Record = fn(&Cell<u64>, u64);

struct Leaf<'a>(&'a Cell<u64>, u64, Record);

impl Drop for Leaf<'_> {
    #[inline(always)]
    fn drop(&mut self) {
        (self.2)(self.0, self.1);
    }
}

struct Pair<'a> {
    first: Leaf<'a>,
    second: Leaf<'a>,
}

impl Drop for Pair<'_> {
    #[inline(always)]
    fn drop(&mut self) {
        (self.first.2)(self.first.0, 9);
    }
}

struct Envelope<'a> {
    pair: Pair<'a>,
    tail: Leaf<'a>,
}

impl Drop for Envelope<'_> {
    #[inline(always)]
    fn drop(&mut self) {
        (self.tail.2)(self.tail.0, 4);
    }
}

#[inline(always)]
fn dispose(value: Envelope<'_>) {
    drop(value);
}

#[inline(never)]
fn append(events: &Cell<u64>, digit: u64) {
    events.set(events.get() * 10 + digit);
}

fn main() {
    let events = Cell::new(0);
    let record = std::hint::black_box(append as Record);
    let value = Envelope {
        pair: Pair {
            first: Leaf(&events, 1, record),
            second: Leaf(&events, 2, record),
        },
        tail: Leaf(&events, 3, record),
    };
    assert_eq!(value.pair.second.1, 2);
    dispose(value);
    assert_eq!(events.get(), 49123);
}
