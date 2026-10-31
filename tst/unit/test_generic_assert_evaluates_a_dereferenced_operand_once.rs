#![feature(generic_assert)]

use std::cell::Cell;

fn main() {
    let value = 17;
    let calls = Cell::new(0);
    assert!(*{
        calls.set(calls.get() + 1);
        &value
    } == 17);
    assert_eq!(calls.get(), 1);
}
