#![feature(inherent_associated_types)]
#![allow(incomplete_features)]
struct Holder<T>(T);
impl<T> Holder<T> { type Buffer<const N: usize> = [T; N]; }
fn repeat<T: Copy, const N: usize>(value: T) -> Holder<T>::Buffer<N> { [value; N] }
fn main() {
    let first: Holder<u8>::Buffer<3> = repeat(7u8);
    let second: Holder<u16>::Buffer<2> = repeat(19u16);
    assert_eq!(first, [7; 3]);
    assert_eq!(second, [19; 2]);
}
