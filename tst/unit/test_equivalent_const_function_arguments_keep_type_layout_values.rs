//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

#[inline(never)]
fn answer<const N: usize>() -> usize { N + 17 }

#[inline(never)]
fn through_layout<T>() -> (usize, usize)
where [(); std::mem::size_of::<T>()]:, [(); std::mem::align_of::<T>()]:
{
    (answer::<{ std::mem::size_of::<T>() }>(),
     answer::<{ std::mem::align_of::<T>() }>())
}

fn main() {
    assert_eq!(answer::<2>(), 19);
    assert_eq!(through_layout::<u16>(), (19, 19));
    assert_eq!(through_layout::<[u16; 3]>(), (23, 19));
    assert_eq!(answer::<6>(), 23);
}
