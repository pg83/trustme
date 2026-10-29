//@ compile-fail: Failed to locate associated value
#![feature(type_alias_impl_trait)]
use std::marker::PhantomData;
struct Pair<A, B>(PhantomData<(A, B)>);
impl<T: Copy> Pair<T, T> { const WIDTH: usize = std::mem::size_of::<T>(); }
type Bytes<const N: usize> = impl Copy;
#[define_opaque(Bytes)]
fn bytes<const N: usize>() -> Bytes<N> { [13u8; N] }
fn main() {
    assert_eq!(Pair::<Bytes<3>, Bytes<7>>::WIDTH, 3);
    assert_eq!(Pair::<Bytes<7>, Bytes<7>>::WIDTH, 7);
    let _ = bytes::<3>();
}
