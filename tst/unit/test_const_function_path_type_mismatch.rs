//@ compile-fail: Type mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn width<T>() -> usize { std::mem::size_of::<T>() }

fn change<T, U>(value: [u8; width::<T>()]) -> [u8; width::<U>()]
where
    [(); width::<T>()]:,
    [(); width::<U>()]:,
{
    value
}

fn main() {}
