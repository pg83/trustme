//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, text: &core::ffi::CStr) -> usize { n + text.to_bytes().len() }
fn keep<const N: usize>(value: [u8; width(N, c"fir")]) -> [u8; width(N, c"fir")] { value }
fn main() {
    assert_eq!(keep::<1>([97, 101, 103, 107]), [97, 101, 103, 107]);
    assert_eq!(keep::<0>([109, 113, 127]), [109, 113, 127]);
}
