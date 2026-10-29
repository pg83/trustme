//@ compile-fail: A constant may not refer to interior mutable data
use std::cell::Cell;
fn main() {
    let _ = const { let value = Cell::new(7u8); &move || value.get() };
}
