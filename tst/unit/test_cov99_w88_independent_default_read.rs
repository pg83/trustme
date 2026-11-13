//@ edition: 2021
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn read<const K: usize>() -> usize {
    K
}

type Lengths<const N: usize, const M: usize = { 2 }> =
    ([u8; N], [u8; read::<M>()]);

fn main() {
    let value: Lengths<_> = ([7u8; 3], [9u8; 2]);
    assert_eq!(value.0.len() + value.1.len(), 5);
}
