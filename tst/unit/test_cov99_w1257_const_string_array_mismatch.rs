//@ compile-fail: mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const TEXT: &str = "x";

const fn width<const N: usize>(text: &str) -> usize {
    N + text.len()
}

fn compare<const N: usize>(value: [(); width::<N>(TEXT)])
where
    [(); width::<N>(TEXT)]: Sized,
    [(); width::<N>("yy")]: Sized,
{
    let _: [(); width::<N>("yy")] = value;
}

fn main() {}
