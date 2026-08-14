#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use std::{mem, ptr};

fn split_first<T, const N: usize>(array: [T; N]) -> (T, [T; N - 1])
where
    [T; N - 1]: Sized,
{
    let array = mem::ManuallyDrop::new(array);
    unsafe {
        let head = ptr::read(&array[0]);
        let tail = ptr::read(&array[1..] as *const [T] as *const [T; N - 1]);
        (head, tail)
    }
}

fn main() {
    let (head, tail) = split_first([0, 1, 2, 3, 4]);
    assert_eq!(head, 0);
    assert_eq!(tail, [1, 2, 3, 4]);
}
