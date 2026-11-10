#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};

#[derive(Clone, Copy)]
struct Row {
    tag: u16,
    payload: u16,
}

fn main() {
    let mut task = Box::pin(#[coroutine] || {
        let index = std::hint::black_box(1usize);
        let rows = std::hint::black_box([
            Row { tag: 3, payload: 17 },
            Row { tag: 5, payload: 43 },
        ]);
        yield 7u16;
        let selected = Row { tag: 73, ..rows[index] };
        yield selected.payload;
        selected.tag + rows[index - 1].payload
    });

    assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(7));
    assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(43));
    assert_eq!(task.as_mut().resume(()), CoroutineState::Complete(90));
}
