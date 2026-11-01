//@ compile-variant: -O
#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]
use std::cell::Cell;
use std::ops::{Coroutine, CoroutineState};
struct Token<'a>(&'a Cell<u32>, u32);
impl Drop for Token<'_> { fn drop(&mut self) { self.0.set(self.0.get() + self.1); } }
fn main() {
    for pair in [(0u8, 5u8), (5, 0), (0, 0), (3, 5)] {
        for gate in [false, true] {
            for finish in [false, true] {
                let events = Cell::new(0);
                let events_ref = &events;
                let mut task = Box::pin(#[coroutine] move || {
                    let tokens = (Token(events_ref, 3), Token(events_ref, 7));
                    match pair {
                        (0, _) | (_, 0) if {
                            if gate {
                                drop(tokens.0);
                                yield 11u8;
                                return 23u8;
                            }
                            false
                        } => unreachable!(),
                        _ => { yield 13u8; 29u8 }
                    }
                });
                let entered = gate && (pair.0 == 0 || pair.1 == 0);
                assert_eq!(task.as_mut().resume(()), CoroutineState::Yielded(if entered { 11 } else { 13 }));
                assert_eq!(events.get(), if entered { 3 } else { 0 });
                if finish {
                    assert_eq!(task.as_mut().resume(()), CoroutineState::Complete(if entered { 23 } else { 29 }));
                    assert_eq!(events.get(), 10);
                }
                drop(task);
                assert_eq!(events.get(), 10);
            }
        }
    }
}
