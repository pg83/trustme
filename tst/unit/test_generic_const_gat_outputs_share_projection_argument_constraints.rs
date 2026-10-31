#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
use std::marker::PhantomData;
const fn count(n: usize) -> usize { n + 1 }
struct Buffer<const N: usize>([u8; N]);
trait Family {
    type Item<const N: usize>;
    fn make<const N: usize>() -> Self::Item<N>;
}
struct Maker;
impl Family for Maker {
    type Item<const N: usize> = Buffer<N>;
    fn make<const N: usize>() -> Buffer<N> { Buffer([N as u8; N]) }
}
struct Source<F, const N: usize>(PhantomData<F>);
trait Produce { type Output; fn produce(self) -> Self::Output; }
impl<F: Family, const N: usize> Produce for Source<F, N> where [(); count(N)]: {
    type Output = F::Item<{ count(N) }>;
    fn produce(self) -> Self::Output { F::make::<{ count(N) }>() }
}
fn relay<F: Family, const N: usize>(source: Source<F, N>) -> F::Item<{ count(N) }>
where [(); count(N)]: { source.produce() }
fn main() { let value = relay(Source::<Maker, 3>(PhantomData)); assert_eq!(value.0, [4u8; 4]); }
