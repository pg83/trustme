#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;

fn main() {
    let mut coroutine = #[coroutine]
    static || {
        yield;
    };
    let mut coroutine = unsafe { Pin::new_unchecked(&mut coroutine) };

    assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Yielded(()));
    assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Complete(()));
}
