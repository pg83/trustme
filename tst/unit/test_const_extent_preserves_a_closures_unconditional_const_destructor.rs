#![feature(generic_const_exprs, const_trait_impl, const_destruct, const_closures)]
#![allow(incomplete_features)]

use std::marker::Destruct;

struct Token;

impl const Drop for Token {
    fn drop(&mut self) {}
}

const fn extent<T: const Destruct>() -> usize {
    assert!(std::mem::size_of::<T>() == 0);
    let temporary = unsafe { std::mem::zeroed::<T>() };
    drop(temporary);
    3
}

const fn fill<F: const Destruct>(value: F) -> [u8; extent::<F>()]
where
    [(); extent::<F>()]:,
{
    drop(value);
    [7; extent::<F>()]
}

const BYTES: [u8; 3] = {
    let token = Token;
    let closure = const move || {
        drop(token);
        13usize
    };
    fill(closure)
};

fn main() {
    assert_eq!(BYTES, [7, 7, 7]);
}
