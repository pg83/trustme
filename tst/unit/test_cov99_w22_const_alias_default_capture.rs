//@ edition: 2021
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn forward<const N: usize>() -> usize {
    N
}

type Pair<const N: usize, const M: usize = { forward::<N>() }> = ([u8; N], [u8; M]);

fn main() {
    let pair: Pair<_> = ([1; 3], [2; 3]);
    assert_eq!(pair.0.len(), 3);
    assert_eq!(pair.1.len(), 3);
}
