#![feature(type_alias_impl_trait)]
use std::marker::PhantomData;
struct Pair<A, B>(PhantomData<(A, B)>);
impl<T: Copy> Pair<T, T> { const WIDTH: usize = std::mem::size_of::<T>(); }
type Bytes<const N: usize> = impl Copy;
#[define_opaque(Bytes)]
fn bytes<const N: usize>() -> Bytes<N> { [13u8; N] }
fn width<const N: usize>() -> usize { Pair::<Bytes<N>, Bytes<N>>::WIDTH }
fn main() {
    assert_eq!(Pair::<Bytes<3>, Bytes<3>>::WIDTH, 3);
    assert_eq!(Pair::<Bytes<7>, Bytes<7>>::WIDTH, 7);
    assert_eq!(Pair::<Bytes<{1 + 2}>, Bytes<3>>::WIDTH, 3);
    assert_eq!(width::<5>(), 5);
    let _ = bytes::<3>();
}
