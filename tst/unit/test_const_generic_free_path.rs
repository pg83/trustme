#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn value<const N: usize>() -> usize {
    N
}

fn call<const M: usize>() -> usize
where
    [(); M + 1]: Sized,
{
    value::<{ M + 1 }>()
}

fn main() {
    assert!(call::<2>() == 3);
}
