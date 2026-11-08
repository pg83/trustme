#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;

fn drive<R, G: Coroutine<R> + Unpin>(mut task: G, first: R, second: R) -> (G::Yield, G::Return) {
    let yielded = match Pin::new(&mut task).resume(first) {
        CoroutineState::Yielded(value) => value,
        CoroutineState::Complete(_) => panic!("completed before yielding"),
    };
    let returned = match Pin::new(&mut task).resume(second) {
        CoroutineState::Complete(value) => value,
        CoroutineState::Yielded(_) => panic!("yielded instead of completing"),
    };
    (yielded, returned)
}

fn main() {
    let task = #[coroutine] |first| {
        let second = yield first;
        second
    };
    assert_eq!(drive(task, 31u16, 47u16), (31, 47));
}
