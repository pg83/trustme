#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const ENABLED: bool = true;

const fn count(n: usize, enabled: bool) -> usize {
    n + enabled as usize
}

fn preserve<const N: usize>(value: [u8; count(N, ENABLED)]) -> [u8; count(N, true)]
where
    [(); count(N, ENABLED)]:,
    [(); count(N, true)]:,
{
    value
}

fn main() {
    assert_eq!(preserve::<2>([7, 8, 9]), [7, 8, 9]);
}
