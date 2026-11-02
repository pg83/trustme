//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, extra: f32) -> usize { n + extra as usize }
fn keep<const N: usize>(value: [u8; width(N, 1.75f32)]) -> [u8; width(N, 1.75f32)] { value }
fn main() {
    assert_eq!(keep::<2>([11, 13, 17]), [11, 13, 17]);
    assert_eq!(keep::<0>([19]), [19]);
}
