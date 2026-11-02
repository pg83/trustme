//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, extra: bool) -> usize { n + extra as usize }
fn keep<const N: usize>(value: [u8; width(N, true)]) -> [u8; width(N, true)] { value }
fn main() {
    assert_eq!(keep::<2>([43, 47, 53]), [43, 47, 53]);
    assert_eq!(keep::<0>([59]), [59]);
}
