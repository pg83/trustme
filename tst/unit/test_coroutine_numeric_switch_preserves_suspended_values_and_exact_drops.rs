//@ compile-variant: -O
#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::cell::Cell;
use std::ops::{Coroutine, CoroutineState};

struct Token<'a>(&'a Cell<u32>);
impl Drop for Token<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }

fn main() {
    for selector in [0u16, 7, 19, 31] {
        for finish in [false, true] {
            let drops = Cell::new(0);
            let drops_ref = &drops;
            let mut task = Box::pin(#[coroutine] move || {
                let token = Token(drops_ref);
                let saved = selector + 200;
                yield saved;
                let adjustment = match selector { 0 => 3, 7 => 11, 19 => 23, _ => 37 };
                let result = saved + adjustment;
                drop(token);
                result
            });
            assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(selector + 200));
            assert_eq!(drops.get(), 0);
            if finish {
                let adjustment = match selector { 0 => 3, 7 => 11, 19 => 23, _ => 37 };
                assert_eq!(task.as_mut().resume(()), CoroutineState::Complete(selector + 200 + adjustment));
            }
            drop(task);
            assert_eq!(drops.get(), 1);
        }
    }
}
