// `yield` evaluates to the argument the coroutine was resumed with, and
// `value.yield` is `yield value`. `gen { .. }` written out is a generator, so
// `gen` is a keyword from Rust 2024 on.
//@ edition: 2024
#![feature(gen_blocks, coroutines, coroutine_trait, yield_expr, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};
use std::pin::pin;

fn main() {
    let mut gn = gen {
        yield 1;
        2.yield;
    };
    assert_eq!(gn.next(), Some(1));
    assert_eq!(gn.next(), Some(2));
    assert_eq!(gn.next(), None);

    let mut coro = pin!(
        #[coroutine]
        |_: i32| {
            let x = yield 1;
            let y = (x + 2).yield;
            y + 10
        }
    );
    assert_eq!(coro.as_mut().resume(0), CoroutineState::Yielded(1));
    assert_eq!(coro.as_mut().resume(2), CoroutineState::Yielded(4));
    assert_eq!(coro.as_mut().resume(3), CoroutineState::Complete(13));
}
