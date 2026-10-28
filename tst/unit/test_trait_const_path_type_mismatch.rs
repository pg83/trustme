//@ compile-fail: Type mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Width { const N: usize; }

fn change<T: Width, U: Width>(value: [u8; <T as Width>::N]) -> [u8; <U as Width>::N]
where
    [(); <T as Width>::N]:,
    [(); <U as Width>::N]:,
{
    value
}

fn main() {}
