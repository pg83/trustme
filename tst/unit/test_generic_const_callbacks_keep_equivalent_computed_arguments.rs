//@ compile-variant: -O
#![feature(generic_const_exprs, generic_const_items)]
#![allow(incomplete_features)]

#[inline(never)]
fn answer<const N: usize>() -> usize { N + 29 }

const CALLBACK<const N: usize>: fn() -> usize = answer::<N>;

#[inline(never)]
fn through_layout<T>() -> (fn() -> usize, fn() -> usize)
where [(); std::mem::size_of::<T>()]:
{
    (CALLBACK::<{ std::mem::size_of::<T>() }>, CALLBACK::<2>)
}

fn main() {
    assert_eq!(CALLBACK::<2>(), 31);
    let (computed, explicit) = through_layout::<u16>();
    assert_eq!((computed(), explicit()), (31, 31));
    let (computed, explicit) = through_layout::<[u16; 3]>();
    assert_eq!((computed(), explicit()), (35, 31));
}
