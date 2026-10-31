#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
#[inline(never)]
fn inspect<const N: usize>(value: &[u16; N + 1], index: usize) -> u16 where [(); N + 1]: {
    value[index]
}
fn main() {
    assert_eq!(inspect::<2>(&[37, 41, 43], std::hint::black_box(2)), 43);
    assert_eq!(inspect::<4>(&[3, 5, 7, 11, 13], std::hint::black_box(3)), 11);
}
