//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata

#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Buffer<const N: usize, const M: usize> {
    first: [u8; N],
    second: [u8; M],
}

impl<const N: usize, const M: usize> Buffer<N, M> {
    fn new(first: [u8; N], second: [u8; M]) -> Self {
        Self { first, second }
    }
}

type Alias<const N: usize, const M: usize = { { N } }> = Buffer<N, M>;

pub fn construct() -> usize {
    let value = Alias::<_>::new([7u8; 3], [9u8; 3]);
    value.first.len() + value.second.len()
}
