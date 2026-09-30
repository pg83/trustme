// Inside `fn run_tests<L, D>() where W<L>: Concat<L, Output = W<D>>`, the call
// `test(W::ONE, W::ONE)` leaves `test`'s parameters to the obligation
// `W<?L>: Concat<?L, Output = W<?D>>`. The where-clause and two impls may all
// apply; rustc drops impl candidates in favour of a where-clause that is not
// global (winnowing), so the where-clause's `L` and `D` are inferred. We
// held a where-clause that binds the goal's variables to be ambiguous, kept
// the impls beside it, and reported "type annotations needed".
use std::marker::PhantomData;
pub struct W<T>(PhantomData<T>);
impl<T> W<T> {
    pub const ONE: Self = W(PhantomData);
}
pub trait Concat<H> {
    type Output;
    fn concat(&self, hi: &W<H>) -> Self::Output;
}
impl Concat<u8> for W<u8> {
    type Output = W<u16>;
    fn concat(&self, _: &W<u8>) -> W<u16> { W(PhantomData) }
}
impl Concat<u16> for W<u16> {
    type Output = W<u32>;
    fn concat(&self, _: &W<u16>) -> W<u32> { W(PhantomData) }
}
fn test<L, D>(lhs: W<L>, rhs: W<L>) -> usize
where
    W<L>: Concat<L, Output = W<D>>,
{
    let _wide: W<D> = lhs.concat(&rhs);
    std::mem::size_of::<D>()
}
fn run_tests<L, D>() -> usize
where
    W<L>: Concat<L, Output = W<D>>,
{
    test(W::ONE, W::ONE)
}
fn main() {
    assert_eq!(run_tests::<u8, u16>(), 2);
    assert_eq!(run_tests::<u16, u32>(), 4);
}
