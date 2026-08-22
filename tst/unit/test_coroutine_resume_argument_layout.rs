#![feature(coroutine_trait, coroutines, stmt_expr_attributes)]
#![allow(dropping_copy_types)]

use std::mem::size_of_val;
use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;

fn main() {
    let copy_argument = #[coroutine] |mut value: usize| {
        loop {
            drop(value);
            value = yield;
        }
    };
    let move_argument = #[coroutine] |mut value: Box<usize>| {
        loop {
            drop(value);
            value = yield;
        }
    };

    // A resume argument is supplied afresh on every resume and is not stored
    // across the suspension, so only the one-byte discriminant remains.
    assert_eq!(size_of_val(&copy_argument), 1);
    assert_eq!(size_of_val(&move_argument), 1);

    let mut copy_argument = copy_argument;
    let mut copy_argument = unsafe { Pin::new_unchecked(&mut copy_argument) };
    assert_eq!(copy_argument.as_mut().resume(11), CoroutineState::Yielded(()));
    assert_eq!(copy_argument.as_mut().resume(12), CoroutineState::Yielded(()));

    let mut move_argument = move_argument;
    let mut move_argument = unsafe { Pin::new_unchecked(&mut move_argument) };
    assert_eq!(move_argument.as_mut().resume(Box::new(13)), CoroutineState::Yielded(()));
    assert_eq!(move_argument.as_mut().resume(Box::new(14)), CoroutineState::Yielded(()));
}
