#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;

fn preserve<T>(value: T) -> T {
    value
}

fn main() {
    let mut task = preserve(#[coroutine] |initial: u16| -> Option<u32> {
        let resumed: u16 = yield (initial as u8, initial > 255);
        Some(initial as u32 + resumed as u32)
    });

    assert_eq!(
        Pin::new(&mut task).resume(513u16),
        CoroutineState::Yielded((1u8, true)),
    );
    assert_eq!(
        Pin::new(&mut task).resume(17u16),
        CoroutineState::Complete(Some(530u32)),
    );
}
