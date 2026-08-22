#![feature(coroutine_trait, coroutines, stmt_expr_attributes)]

use std::mem::size_of_val;
use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;

fn main() {
    let coroutine = #[coroutine] || {
        {
            let first: i32 = 4;
            yield;
            assert_eq!(first, 4);
        }
        {
            let second: i32 = 5;
            yield;
            assert_eq!(second, 5);
        }
        {
            let third: i32 = 6;
            yield;
            assert_eq!(third, 6);
        }
        {
            let fourth: i32 = 7;
            yield;
            assert_eq!(fourth, 7);
        }
    };

    // The four locals are live in disjoint suspension states and share one
    // four-byte storage slot next to the four-byte coroutine discriminant.
    assert_eq!(size_of_val(&coroutine), 8);

    let mut coroutine = coroutine;
    let mut coroutine = unsafe { Pin::new_unchecked(&mut coroutine) };
    for _ in 0..4 {
        assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Yielded(()));
    }
    assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Complete(()));
}
