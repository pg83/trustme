#![feature(coroutines, stmt_expr_attributes)]

use std::mem::size_of_val;

fn take<T>(_: T) {}

fn main() {
    let captured = false;
    let coroutine = #[coroutine] || {
        yield;
        take(captured);
    };

    let plain_size = size_of_val(&coroutine);
    let optional_size = size_of_val(&Some(coroutine));
    assert!(optional_size > plain_size);
}
