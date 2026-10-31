//@ compile-fail: Type mismatch
use std::marker::PhantomData;
struct Buffer<const N: usize>([u8; N]);
trait Family {
    type Item<const N: usize>;
    fn make<const N: usize>() -> Self::Item<N>;
}
struct Source<F, const N: usize>(PhantomData<F>);
trait Produce { type Output; fn produce(self) -> Self::Output; }
impl<F: Family, const N: usize> Produce for Source<F, N> {
    type Output = F::Item<N>;
    fn produce(self) -> Self::Output { F::make::<N>() }
}
fn relay<F: Family, const N: usize>(source: Source<F, N>) -> Buffer<N> { source.produce() }
fn main() {}
