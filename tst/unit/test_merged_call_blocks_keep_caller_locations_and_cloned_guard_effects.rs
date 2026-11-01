//@ compile-variant: -O
use std::cell::Cell;
#[track_caller]
#[inline(never)]
fn origin() -> u32 { std::panic::Location::caller().line() }
#[inline(never)]
fn route(first: bool) -> u32 {
    if first {
        origin()
    } else {
        origin()
    }
}
#[track_caller]
#[inline(never)]
fn accepted(value: u16, events: &Cell<u16>) -> bool {
    events.set(events.get() + 1);
    value == 7
}
fn guarded(pair: (u16, u16), events: &Cell<u16>) -> u16 {
    match pair { (0, value) | (value, 0) if accepted(value, events) => value, _ => 3 }
}
fn main() {
    let first = route(true);
    let second = route(false);
    assert!(0 < first && first < second);
    let events = Cell::new(0);
    assert_eq!(guarded((0, 7), &events), 7);
    assert_eq!(guarded((7, 0), &events), 7);
    assert_eq!(guarded((0, 5), &events), 3);
    assert_eq!(events.get(), 3);
}
