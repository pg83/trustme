#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};

fn main() {
    let captured = std::hint::black_box(17u32);
    let task = #[coroutine] move |initial: u32| yield initial + captured;
    let mut task = Box::pin(task);
    assert_eq!(task.as_mut().resume(44), CoroutineState::Yielded(61));
    assert_eq!(task.as_mut().resume(83), CoroutineState::Complete(83));
}
