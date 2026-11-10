#![warn(unused_must_use, unsafe_code)]

use std::cell::Cell;

#[must_use]
fn record(counter: &Cell<u16>) -> u16 {
    counter.set(counter.get() + 1);
    17
}

fn main() {
    let counter = Cell::new(0);
    record(&counter);

    let mut value = 7u16;
    unsafe {
        std::ptr::write_volatile(&mut value, 13);
    }

    assert_eq!(counter.get(), 1);
    assert_eq!(value, 13);
}
