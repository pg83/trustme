//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, extra: f64) -> usize { n + extra as usize }
fn keep<const N: usize>(value: [u16; width(N, 2.5f64)]) -> [u16; width(N, 2.5f64)] { value }
fn main() {
    assert_eq!(keep::<1>([23, 29, 31]), [23, 29, 31]);
    assert_eq!(keep::<0>([37, 41]), [37, 41]);
}
