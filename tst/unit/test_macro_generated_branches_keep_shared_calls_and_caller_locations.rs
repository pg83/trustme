//@ compile-variant: -O
#![feature(core_intrinsics)]
use std::cell::Cell;
#[track_caller]
#[inline(never)]
fn record(value: u32, events: &Cell<u32>) -> (u32, u32) {
    events.set(events.get() + 1);
    (value.wrapping_add(7), std::panic::Location::caller().line())
}
macro_rules! routes {
    () => {
        const GENERATED_AT: u32 = line!();
        #[inline(never)]
        fn tracked(first: bool, value: u32, events: &Cell<u32>) -> (u32, u32) {
            if first { return record(value, events); } else { return record(value, events); }
        }
        #[inline(never)]
        fn indirect(first: bool, target: fn(u32) -> u32, value: u32) -> u32 {
            if first { return target(value); } else { return target(value); }
        }
        #[inline(never)]
        fn count(first: bool, value: u32) -> u32 {
            if first { return std::intrinsics::ctpop(value); } else { return std::intrinsics::ctpop(value); }
        }
    };
}
routes!();
fn tripled(value: u32) -> u32 { value.wrapping_mul(3) }
fn main() {
    let events = Cell::new(0);
    let target = std::hint::black_box(tripled as fn(u32) -> u32);
    for first in [false, true] {
        assert_eq!(tracked(first, 13, &events), (20, GENERATED_AT));
        assert_eq!(indirect(first, target, 13), 39);
        assert_eq!(count(first, 0b10110), 3);
    }
    assert_eq!(events.get(), 2);
}
