//@ edition: 2021
//@ compile-flags: -C opt-level=0

#![feature(generic_const_items, generic_const_exprs)]
#![allow(incomplete_features)]

const CHAIN<const N: usize>: &'static [usize; 2] =
    &[N, (N != 0 && CHAIN::<N>[0] != 0) as usize];

const WARM: usize = CHAIN::<0>[0];

#[inline(never)]
fn get<const N: usize>() -> &'static [usize; 2]
where
    [(); N + 0]:,
{
    CHAIN::<{ N + 0 }>
}

fn main() {
    assert_eq!(WARM, 0);
    assert_eq!(*get::<0>(), [0, 0]);
}
