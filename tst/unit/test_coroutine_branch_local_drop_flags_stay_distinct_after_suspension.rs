//@ default-compile-flags: -O
#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]
use std::cell::Cell;
use std::ops::{Coroutine, CoroutineState};
struct Token<'a>(&'a Cell<u32>, u32);
impl Drop for Token<'_> { fn drop(&mut self) { self.0.set(self.0.get() + self.1); } }
fn main() {
    for side in [false, true] {
        for initialized in [false, true] {
            for finish in [false, true] {
                let events = Cell::new(0);
                let count = &events;
                let mut task = Box::pin(#[coroutine] move || {
                    if side {
                        let _left;
                        if initialized { _left = Token(count, 3); }
                        yield 7u8;
                    } else {
                        let _right;
                        if initialized { _right = Token(count, 5); }
                        yield 7u8;
                    }
                    11u8
                });
                assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(7));
                assert_eq!(events.get(), 0);
                if finish { assert_eq!(task.as_mut().resume(()), CoroutineState::Complete(11)); }
                drop(task);
                assert_eq!(events.get(), if initialized { if side { 3 } else { 5 } } else { 0 });
            }
        }
    }
}
