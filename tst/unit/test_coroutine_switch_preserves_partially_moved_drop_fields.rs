//@ compile-variant: -O
#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::cell::Cell;
use std::ops::{Coroutine, CoroutineState};

struct Token<'a>(&'a Cell<u8>, u8);
impl Drop for Token<'_> {
    fn drop(&mut self) {
        assert_eq!(self.0.get() & self.1, 0);
        self.0.set(self.0.get() | self.1);
    }
}

fn main() {
    for selector in [2u32, 5, 11, 17] {
        for finish in [false, true] {
            let drops = Cell::new(0);
            let drops_ref = &drops;
            let mut task = Box::pin(#[coroutine] move || {
                let pair = (Token(drops_ref, 1), Token(drops_ref, 2));
                match selector {
                    2 => { drop(pair.0); yield 13u8; }
                    5 => { drop(pair.1); yield 19u8; }
                    11 => { yield 23u8; drop(pair.0); }
                    _ => { yield 29u8; }
                }
                41u8
            });
            let expected = match selector { 2 => 13, 5 => 19, 11 => 23, _ => 29 };
            assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(expected));
            assert_eq!(drops.get(), match selector { 2 => 1, 5 => 2, _ => 0 });
            if finish { assert_eq!(task.as_mut().resume(()), CoroutineState::Complete(41)); }
            drop(task);
            assert_eq!(drops.get(), 3);
        }
    }
}
