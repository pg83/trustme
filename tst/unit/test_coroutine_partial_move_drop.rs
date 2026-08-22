#![feature(coroutine_trait, coroutines, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};

static mut LEFT_DROPS: usize = 0;
static mut RIGHT_DROPS: usize = 0;

struct Left;

impl Drop for Left {
    fn drop(&mut self) {
        unsafe { LEFT_DROPS += 1 };
    }
}

struct Right;

impl Drop for Right {
    fn drop(&mut self) {
        unsafe { RIGHT_DROPS += 1 };
    }
}

struct Pair {
    left: Left,
    right: Right,
}

fn main() {
    let mut coroutine = Box::pin(#[coroutine] || {
        yield drop(Pair { left: Left, right: Right }.left);
    });

    assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Yielded(()));
    let left_drops = unsafe { LEFT_DROPS };
    let right_drops = unsafe { RIGHT_DROPS };
    assert_eq!(left_drops, 1);
    assert_eq!(right_drops, 0);

    drop(coroutine);
    let left_drops = unsafe { LEFT_DROPS };
    let right_drops = unsafe { RIGHT_DROPS };
    assert_eq!(left_drops, 1);
    assert_eq!(right_drops, 1);
}
