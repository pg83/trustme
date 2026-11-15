//@ edition: 2021
//@ compile-flags: -C opt-level=0

#![feature(generic_const_items, generic_const_exprs)]
#![allow(incomplete_features)]

const CHAIN<const N: usize>: &'static u32 = &if N == 0 {
    37
} else {
    *CHAIN::<0>
};

const WARM: u32 = *CHAIN::<0>;

#[inline(never)]
fn get<const N: usize>() -> &'static u32
where
    [(); N + 0]:,
{
    CHAIN::<{ N + 0 }>
}

fn main() {
    assert_eq!(WARM, 37);
    assert_eq!(*get::<0>(), 37);
}
