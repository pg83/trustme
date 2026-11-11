#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::cell::Cell;
use std::ops::{Coroutine, CoroutineState};

struct Mark<'a>(&'a Cell<u32>, u32);

impl Drop for Mark<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() * 10 + self.1);
    }
}

fn inspect(transfer: bool, finish: bool) {
    let events = Cell::new(0);
    let mut task = Box::pin(#[coroutine] || {
        let pair = (Mark(&events, 2), Mark(&events, 3));
        let transferred = if transfer { Some(pair.0) } else { None };
        drop(transferred);
        yield events.get();
        drop(pair.1);
    });
    assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(if transfer { 2 } else { 0 }));
    if finish {
        assert_eq!(task.as_mut().resume(()), CoroutineState::Complete(()));
    }
    drop(task);
    assert_eq!(events.get(), if finish && !transfer { 32 } else { 23 });
}

fn main() {
    for transfer in [false, true] {
        for finish in [false, true] {
            inspect(transfer, finish);
        }
    }
}
