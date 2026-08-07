#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::mem::size_of_val;
use std::ops::Coroutine;

fn require_unit_coroutine<T: Coroutine<(), Yield = (), Return = ()>>(_: &T) {}

fn main() {
    let coroutine = #[coroutine] || {
        yield;
    };
    require_unit_coroutine(&coroutine);
    assert!(size_of_val(&coroutine) > 0);
}
