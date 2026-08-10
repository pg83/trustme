#![feature(unsize)]

use std::marker::Unsize;

fn as_slice<A, T>(value: &A) -> &[T]
where
    A: Unsize<[T]>,
{
    value
}

fn main() {
    let values = [10_u8, 20, 30, 40];
    let slice = as_slice(&values);

    assert_eq!(slice.len(), 4);
    assert_eq!(slice, &[10, 20, 30, 40]);
}
