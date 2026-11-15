//@ compile-fail: cycle detected when evaluating constant
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Marker {}
impl<T> Marker for T {}

const CYCLE: usize = cycle::<0>();

const fn need_marker<T: Marker>() {}

const fn cycle<const N: usize>() -> usize
where
    [(); 0 + N]: Marker,
{
    need_marker::<[(); CYCLE + N]>();
    0
}

fn main() {}
