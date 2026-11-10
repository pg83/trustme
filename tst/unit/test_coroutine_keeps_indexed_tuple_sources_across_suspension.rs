#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};

fn main() {
    let mut task = Box::pin(#[coroutine] || {
        let index = std::hint::black_box(1usize);
        let numbers = std::hint::black_box([17u16, 29, 43]);
        yield 7u16;
        let pair = (numbers[index], numbers[index + 1]);
        yield pair.0;
        pair.1 + numbers[index - 1]
    });

    assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(7));
    assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(29));
    assert_eq!(task.as_mut().resume(()), CoroutineState::Complete(60));
}
