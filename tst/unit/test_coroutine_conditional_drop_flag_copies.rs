#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::cell::Cell;
use std::ops::{Coroutine, CoroutineState};

struct Ticket<'a>(&'a Cell<usize>);

impl Drop for Ticket<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

fn check_forget(first: bool, second: bool, stage: u8) {
    let drops = Cell::new(0);
    let ticket = Ticket(&drops);
    let mut task = Box::pin(#[coroutine] move || {
        let value = ticket;
        yield 3usize;
        if first {
            std::mem::forget(value);
        } else if second {
            std::mem::forget(value);
        }
        yield 7;
        11
    });

    assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(3));
    if stage > 0 {
        assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(7));
        if stage > 1 {
            assert_eq!(task.as_mut().resume(()), CoroutineState::Complete(11));
        }
    }
    drop(task);
    assert_eq!(drops.get(), if stage > 0 && (first || second) { 0 } else { 1 });
}

fn check_init(first: bool, second: bool, stage: u8) {
    let drops = Cell::new(0);
    let drops_ref = &drops;
    let mut task = Box::pin(#[coroutine] move || {
        yield 3usize;
        let _value;
        if first {
            _value = Ticket(drops_ref);
        } else if second {
            _value = Ticket(drops_ref);
        }
        yield 7;
        11
    });

    assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(3));
    if stage > 0 {
        assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(7));
        if stage > 1 {
            assert_eq!(task.as_mut().resume(()), CoroutineState::Complete(11));
        }
    }
    drop(task);
    assert_eq!(drops.get(), if stage > 0 && (first || second) { 1 } else { 0 });
}

fn main() {
    for stage in 0..3 {
        for (first, second) in [(false, false), (true, false), (false, true), (true, true)] {
            check_forget(first, second, stage);
            check_init(first, second, stage);
        }
    }
}
