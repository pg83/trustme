//@ crate-type: lib
//@ compile-fail: Value mismatch between
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

type Narrow<const N: usize, const K: u8> = [u8; N + K as usize];
type Wide<const N: usize, const K: u64> = [u8; N + K as usize];

pub fn unequal<const N: usize>(value: Narrow<N, 1>) -> Wide<N, 2> {
    value
}
