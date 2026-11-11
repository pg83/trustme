#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::ops::{Coroutine, CoroutineState};

fn update(value: &mut Option<u32>, enabled: bool) -> u32 {
    match *value {
        Some(ref mut number) if *number > 40 && enabled => {
            *number += 7;
            *number
        }
        Some(ref number) if *number == 40 => *number + 11,
        Some(ref mut number) => {
            *number += 13;
            *number
        }
        None => 0,
    }
}

fn main() {
    let mut first = Some(43);
    assert_eq!(update(&mut first, true), 50);
    assert_eq!(first, Some(50));
    let mut second = Some(40);
    assert_eq!(update(&mut second, false), 51);
    assert_eq!(second, Some(40));
    let mut third = Some(43);
    assert_eq!(update(&mut third, false), 56);
    assert_eq!(third, Some(56));
    assert_eq!(update(&mut None, true), 0);

    let baseline = 11u32;
    let mut total = 13u32;
    {
        let mut coroutine = std::pin::pin!(#[coroutine] || {
            total += baseline;
            yield total;
            total += baseline;
            total
        });
        assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Yielded(24));
        assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Complete(35));
    }
    assert_eq!(total, 35);
}
