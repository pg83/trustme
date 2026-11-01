//@ compile-variant: -O
#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]
use std::cell::Cell;
use std::ops::{Coroutine, CoroutineState};
struct Token<'a>(&'a Cell<u32>);
impl Drop for Token<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
fn main() {
    for first in [false, true] {
        for second in [false, true] {
            for stop in 1..=3 {
                let events = Cell::new(0);
                let token = Token(&events);
                let mut task = Box::pin(#[coroutine] move || {
                    let original = token;
                    yield 3u8;
                    let _transferred;
                    if first { _transferred = original; }
                    else if second { _transferred = original; }
                    yield 5u8;
                    7u8
                });
                for step in 1..=stop {
                    let expected = match step { 1 => CoroutineState::Yielded(3), 2 => CoroutineState::Yielded(5), _ => CoroutineState::Complete(7) };
                    assert_eq!(task.as_mut().resume(()), expected);
                    assert_eq!(events.get(), if step == 3 { 1 } else { 0 });
                }
                drop(task);
                assert_eq!(events.get(), 1);
            }
        }
    }
}
