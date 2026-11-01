#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]
use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;

fn main() {
    let seed = 19u16;
    let mut outer = #[coroutine] move || {
        let mut inner = #[coroutine] move || { yield seed + 1; seed + 2 };
        let first = match Pin::new(&mut inner).resume(()) {
            CoroutineState::Yielded(value) => value,
            CoroutineState::Complete(_) => 0,
        };
        yield first;
        match Pin::new(&mut inner).resume(()) {
            CoroutineState::Complete(value) => value,
            CoroutineState::Yielded(_) => 0,
        }
    };
    assert_eq!(Pin::new(&mut outer).resume(()), CoroutineState::Yielded(20));
    assert_eq!(Pin::new(&mut outer).resume(()), CoroutineState::Complete(21));
}
