//@ compile-variant: -O
use std::cell::Cell;
#[track_caller]
#[inline(never)]
fn observe(events: &Cell<u32>, condition: bool) -> (bool, u32) {
    events.set(events.get() + 1);
    (condition, std::panic::Location::caller().line())
}
#[inline(never)]
fn guarded(pair: (u8, u8), gate: bool, events: &Cell<u32>) -> u32 {
    match pair {
        (0, _) | (_, 0) if gate && observe(events, true).0 => 11,
        _ => 29,
    }
}
#[inline(never)]
fn chosen(first: bool, condition: bool) -> u8 {
    if first { if condition { 3 } else { 7 } }
    else { if condition { 3 } else { 7 } }
}
fn main() {
    let events = Cell::new(0);
    for pair in [(0, 5), (5, 0), (0, 0), (3, 5)] {
        for gate in [false, true] {
            assert_eq!(guarded(pair, gate, &events), if gate && (pair.0 == 0 || pair.1 == 0) { 11 } else { 29 });
        }
    }
    assert_eq!(events.get(), 3);
    for first in [false, true] {
        for condition in [false, true] { assert_eq!(chosen(first, condition), if condition { 3 } else { 7 }); }
    }
    let expected = line!() + 1;
    assert_eq!(observe(&events, false), (false, expected));
}
