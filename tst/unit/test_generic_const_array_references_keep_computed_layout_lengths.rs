//@ compile-variant: -O
#![feature(generic_const_exprs, generic_const_items)]
#![allow(incomplete_features)]

const ROW<const N: usize>: &'static [u8; N] = &[N as u8; N];

#[inline(never)]
fn through_layout<T>() -> (&'static [u8], &'static [u8])
where [(); std::mem::size_of::<T>()]:
{
    (ROW::<{ std::mem::size_of::<T>() }>, ROW::<2>)
}

fn main() {
    assert_eq!(ROW::<2>, &[2, 2]);
    let (computed, explicit) = through_layout::<u16>();
    assert_eq!((computed, explicit), (&[2, 2][..], &[2, 2][..]));
    let (computed, explicit) = through_layout::<[u16; 3]>();
    assert_eq!((computed, explicit), (&[6; 6][..], &[2, 2][..]));
}
