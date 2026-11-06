#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Width<const N: usize> { const SIZE: usize; }
impl<const N: usize> Width<N> for u16 { const SIZE: usize = N + 1; }

fn bytes<T: Width<N>, const N: usize>(array: [u8; T::SIZE], _: T, _: [(); N]) -> usize
where [(); T::SIZE]: { array.len() }

fn main() {
    assert_eq!(bytes([2, 3, 5, 7], 0u16, [(); 3]), 4);
    assert_eq!(bytes([11], 0u16, [(); 0]), 1);
}
