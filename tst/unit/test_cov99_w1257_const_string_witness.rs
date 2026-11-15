#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const TEXT: &str = "x";

const fn width<const N: usize>(_: &str) -> usize {
    N
}

fn require_sized<T: Sized>() {}

fn compare<const N: usize>()
where
    [(); width::<N>(TEXT)]: Sized,
    [(); width::<N>("x")]: Sized,
{
    require_sized::<[(); width::<N>("x")]>();
}

fn main() {
    compare::<3>();
}
