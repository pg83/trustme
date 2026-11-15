#![feature(transmutability)]

use core::mem::{Assume, TransmuteFrom};

fn retain<F: 'static>(callback: F) -> F
where
    (): TransmuteFrom<
        &'static F,
        { Assume { alignment: false, lifetimes: false, safety: true, validity: false } },
    >,
{
    callback
}

fn main() {
    let callback = retain(|bytes: &[u8]| match bytes {
        [_, middle @ .., _] => middle.len(),
        _ => 0,
    });
    assert_eq!(callback(&[3, 5, 8, 13]), 2);
    assert_eq!(callback(&[21]), 0);
}
