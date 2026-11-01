#![feature(explicit_tail_calls, coroutines, coroutine_trait, stmt_expr_attributes)]
use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;
fn target(value: u16) -> u16 { value + 3 }
fn relay(value: u16) -> u16 {
    let mut generator = #[coroutine] || {
        yield 41u16;
        43u16
    };
    assert_eq!(Pin::new(&mut generator).resume(()), CoroutineState::Yielded(41));
    assert_eq!(Pin::new(&mut generator).resume(()), CoroutineState::Complete(43));
    become target(value)
}
fn main() { assert_eq!(relay(47), 50); }
