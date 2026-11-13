#![feature(generic_const_items, generic_const_exprs)]
#![allow(incomplete_features)]

const VALUE<const N: usize>: usize = N;

#[inline(never)]
fn pair<const N: usize>() -> (usize, usize)
where
    [(); N + 0]:,
{
    (VALUE::<N>, VALUE::<{ N + 0 }>)
}

fn main() {
    assert_eq!(pair::<7>(), (7, 7));
}
