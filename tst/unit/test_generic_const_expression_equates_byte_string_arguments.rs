//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, text: &[u8; 3]) -> usize { n + text.len() }
fn keep<const N: usize>(value: [u8; width(N, b"oak")]) -> [u8; width(N, b"oak")] { value }
fn main() {
    assert_eq!(keep::<1>([131, 137, 139, 149]), [131, 137, 139, 149]);
    assert_eq!(keep::<0>([151, 157, 163]), [151, 157, 163]);
}
