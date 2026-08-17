// `&pin mut T` and `&pin const T` are `Pin<&mut T>` and `Pin<&T>`, in a type, as
// a receiver, and as a borrow of a place.
#![feature(pin_ergonomics)]
#![allow(incomplete_features)]

use std::pin::Pin;

struct Counter(u32);

impl Counter {
    fn read(&pin const self) -> u32 {
        self.0
    }

    fn bump(&pin mut self) -> u32 {
        self.0
    }

    fn read_lt<'a>(&'a pin const self) -> u32 {
        self.0
    }
}

fn read_pinned(value: &pin const Counter) -> u32 {
    value.read()
}

fn bump_pinned(value: &pin mut Counter) -> u32 {
    value.bump()
}

fn takes_pin(value: Pin<&Counter>) -> u32 {
    value.read()
}

/// A pinned mutable reference is passed twice, and once as a shared one: each use
/// reborrows rather than moving.
fn reborrows(value: &pin mut Counter) -> u32 {
    bump_pinned(value) + bump_pinned(value) + read_pinned(value) + takes_pin(value)
}

fn main() {
    let counter = Counter(7);
    let pinned: Pin<&Counter> = &pin const counter;
    assert_eq!(read_pinned(pinned), 7);
    assert_eq!(takes_pin(pinned), 7);
    assert_eq!(pinned.read_lt(), 7);

    let mut other = Counter(9);
    let pinned_mut: Pin<&mut Counter> = &pin mut other;
    assert_eq!(bump_pinned(pinned_mut), 9);

    let mut third = Counter(2);
    assert_eq!(reborrows(&pin mut third), 8);
}
