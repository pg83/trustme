//@ compile-flags: -Cpanic=unwind -Zmir-opt-level=2 -O
use std::cell::Cell;

type Record = fn(&Cell<u32>, u32) -> u32;

struct Mark<'a> {
    events: &'a Cell<u32>,
    record: Record,
}

impl Drop for Mark<'_> {
    #[inline(always)]
    fn drop(&mut self) {
        (self.record)(self.events, 3);
    }
}

#[inline(always)]
extern "C" fn boundary(events: &Cell<u32>, phase: u32, record: Record) -> u32 {
    let _mark = Mark { events, record };
    if phase == 0 {
        record(events, 1)
    } else {
        record(events, 2)
    }
}

#[inline(always)]
fn forward(events: &Cell<u32>, phase: u32, record: Record) -> u32 {
    boundary(events, phase, record) + 5
}

#[inline(never)]
fn append(events: &Cell<u32>, digit: u32) -> u32 {
    events.set(events.get() * 10 + digit);
    digit + 10
}

fn main() {
    let events = Cell::new(0);
    let record = std::hint::black_box(append as Record);
    assert_eq!(forward(&events, std::hint::black_box(0), record), 16);
    assert_eq!(events.get(), 13);
    assert_eq!(forward(&events, std::hint::black_box(1), record), 17);
    assert_eq!(events.get(), 1323);
}
