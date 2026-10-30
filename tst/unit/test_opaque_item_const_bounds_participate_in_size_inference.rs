#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Width<const N: usize> {}
impl<T> Width<{ std::mem::size_of::<T>() }> for T
where [(); std::mem::size_of::<T>()]: {}
trait Count<const N: usize> { fn count(&self) -> usize { N } }
impl<const N: usize> Count<N> for [u8; N] {}
trait Factory { type Item; fn items(self) -> Self::Item; }
impl<const N: usize> Factory for [u8; N] {
    type Item = [u8; N];
    fn items(self) -> Self::Item { self }
}
fn hidden<const N: usize>() -> impl Copy + Factory<Item: Count<N>> { [17u8; N] }
fn measured<T: Factory<Item: Count<N>> + Width<N>, const N: usize>(_: T) -> usize { N }
fn main() {
    let first = hidden::<3>();
    let second = hidden::<7>();
    assert_eq!(measured(first), 3);
    assert_eq!(measured(second), 7);
    assert_eq!(first.items().count(), 3);
    assert_eq!(second.items().count(), 7);
}
