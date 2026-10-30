#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Width<const N: usize> {}
impl<T> Width<{ std::mem::size_of::<T>() }> for T
where [(); std::mem::size_of::<T>()]: {}
trait Count<const N: usize> { fn count(&self) -> usize { N } }
impl<const N: usize> Count<N> for [u8; N] {}
trait Factory { type Item<const M: usize>; fn items<const M: usize>(&self) -> Self::Item<M>; }
impl<const N: usize> Factory for [u8; N] {
    type Item<const M: usize> = [u8; M];
    fn items<const M: usize>(&self) -> Self::Item<M> { [19u8; M] }
}
fn hidden<const N: usize>() -> impl Copy + Factory<Item<N>: Count<N>> { [17u8; N] }
fn measured<T: Factory<Item<N>: Count<N>> + Width<N>, const N: usize>(_: T) -> usize { N }
fn main() {
    let first = hidden::<3>();
    let second = hidden::<7>();
    assert_eq!(measured(first), 3);
    assert_eq!(measured(second), 7);
    assert_eq!(first.items::<3>().count(), 3);
    assert_eq!(second.items::<7>().count(), 7);
}
