//@ compile-fail: A constant may not refer to interior mutable data
#![feature(const_async_blocks)]
use std::cell::Cell;
fn main() {
    let _ = const { let value = Cell::new(7u8); &async move { value.get() } };
}
