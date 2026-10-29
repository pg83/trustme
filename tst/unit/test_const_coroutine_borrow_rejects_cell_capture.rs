//@ compile-fail: A constant may not refer to interior mutable data
#![feature(const_async_blocks, coroutines, stmt_expr_attributes)]
use std::cell::Cell;
fn main() {
    let _ = const { let value = Cell::new(7u8); &(#[coroutine] move || { yield value.get(); }) };
}
