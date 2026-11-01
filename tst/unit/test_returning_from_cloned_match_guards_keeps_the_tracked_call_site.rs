//@ compile-variant: -O
use std::cell::Cell;
#[track_caller]
#[inline(never)]
fn origin(events: &Cell<u32>) -> u32 {
    events.set(events.get() + 1);
    std::panic::Location::caller().line()
}
#[inline(never)]
fn guarded(pair: (u8, u8), gate: bool, events: &Cell<u32>) -> u32 {
    match pair {
        (0, _) | (_, 0) if {
            if gate { return origin(events); }
            false
        } => unreachable!(),
        _ => 0,
    }
}
fn main() {
    let events = Cell::new(0);
    for pair in [(0, 5), (5, 0), (0, 0), (3, 5)] {
        for gate in [false, true] {
            assert_eq!(guarded(pair, gate, &events), if gate && (pair.0 == 0 || pair.1 == 0) { 13 } else { 0 });
        }
    }
    assert_eq!(events.get(), 3);
}
