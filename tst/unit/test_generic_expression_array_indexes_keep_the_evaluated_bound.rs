//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

#[inline(never)]
fn update<const N: usize>(value: u16, index: usize) -> u16
where [(); N * 2 + 1]: {
    let mut values = [value; N * 2 + 1];
    values[index] = value + 3;
    values[0] + values[index] + values[N * 2]
}
fn main() {
    assert_eq!(update::<1>(7, 1), 24);
    assert_eq!(update::<2>(11, 4), 39);
    assert_eq!(update::<0>(13, 0), 48);
}
