#![feature(coroutine_trait)]
#![feature(coroutines)]
#![feature(stmt_expr_attributes)]

use std::ops::Coroutine;

fn require_unit_return<F: Coroutine<String, Yield = String, Return = ()>>(_: &F) {}

fn main() {
    let coroutine = #[coroutine] |value: String| {
        yield value;
    };
    require_unit_return(&coroutine);
}
