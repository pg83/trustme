#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Value<const N: usize>;

impl<const N: usize> Value<N> {
    fn get() -> usize {
        N
    }
}

fn call<const M: usize>() -> usize
where
    [(); M + 1]: Sized,
{
    Value::<{ M + 1 }>::get()
}

fn main() {
    assert!(call::<2>() == 3);
}
