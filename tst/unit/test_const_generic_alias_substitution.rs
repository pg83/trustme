#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

type Alias<const N: usize> = [u8; N + 1];

fn make<const N: usize>() -> Alias<N>
where
    [(); N + 1]:,
{
    [0; N + 1]
}

fn main() {
    assert_eq!(make::<2>().len(), 3);
}
