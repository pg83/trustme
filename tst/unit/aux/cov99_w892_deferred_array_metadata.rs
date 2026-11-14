#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

#[inline(never)]
pub fn at<const N: usize>(array: &[u8; N + 1], index: usize) -> u8
where
    [(); N + 1]:,
{
    array[index]
}
