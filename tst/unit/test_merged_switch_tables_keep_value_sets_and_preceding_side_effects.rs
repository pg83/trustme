//@ compile-variant: -O
use std::cell::Cell;
#[inline(never)]
fn select<const SHIFT: bool>(first: bool, value: u16, events: &Cell<u8>) -> u16 {
    if first {
        events.set(1);
        match value { 3 => 13, 7 => 17, 11 => 19, _ => 23 }
    } else {
        events.set(2);
        if SHIFT {
            match value { 5 => 13, 9 => 17, 13 => 19, _ => 23 }
        } else {
            match value { 3 => 13, 7 => 17, 11 => 19, _ => 23 }
        }
    }
}
fn main() {
    let events = Cell::new(0);
    for (value, normal, shifted) in [(3,13,23), (7,17,23), (11,19,23), (5,23,13), (9,23,17), (13,23,19), (0,23,23)] {
        assert_eq!(select::<false>(true, value, &events), normal);
        assert_eq!(events.get(), 1);
        assert_eq!(select::<false>(false, value, &events), normal);
        assert_eq!(events.get(), 2);
        assert_eq!(select::<true>(true, value, &events), normal);
        assert_eq!(events.get(), 1);
        assert_eq!(select::<true>(false, value, &events), shifted);
        assert_eq!(events.get(), 2);
    }
}
