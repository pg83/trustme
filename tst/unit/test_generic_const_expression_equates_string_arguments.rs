//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, text: &str) -> usize { n + text.len() }
fn keep<const N: usize>(value: [u8; width(N, "elm")]) -> [u8; width(N, "elm")] { value }
fn main() {
    assert_eq!(keep::<1>([61, 67, 71, 73]), [61, 67, 71, 73]);
    assert_eq!(keep::<0>([79, 83, 89]), [79, 83, 89]);
}
