//@ compile-variant: -O
#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::cell::Cell;
use std::ops::{Coroutine, CoroutineState};

struct Token<'a>(&'a Cell<u32>, u32);
impl Drop for Token<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}

fn main() {
    for polls in 1..=4 {
        let events = Cell::new(0);
        let log = &events;
        let mut task = Box::pin(#[coroutine] move |initial: u32| {
            let outer = Token(log, 1);
            let mut value = initial;
            for stage in 0..3 {
                let inner = Token(log, stage + 2);
                value = yield value + stage;
                drop(inner);
            }
            drop(outer);
            value
        });
        assert_eq!(task.as_mut().resume(5), CoroutineState::Yielded(5));
        assert_eq!(events.get(), 0);
        if polls >= 2 {
            assert_eq!(task.as_mut().resume(11), CoroutineState::Yielded(12));
            assert_eq!(events.get(), 2);
        }
        if polls >= 3 {
            assert_eq!(task.as_mut().resume(17), CoroutineState::Yielded(19));
            assert_eq!(events.get(), 23);
        }
        if polls == 4 {
            assert_eq!(task.as_mut().resume(23), CoroutineState::Complete(23));
            assert_eq!(events.get(), 2341);
        }
        drop(task);
        assert_eq!(events.get(), match polls { 1 => 21, 2 => 231, _ => 2341 });
    }
}
