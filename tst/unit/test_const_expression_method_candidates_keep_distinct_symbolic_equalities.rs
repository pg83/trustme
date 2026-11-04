//@ compile-fail: error
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
trait Same<T> {}
impl<T> Same<T> for T {}
struct Holder<const N: usize, const M: usize>;
trait Left { fn measure(&self) -> usize; }
trait Right { fn measure(&self) -> usize; }
impl<const N: usize, const M: usize> Left for Holder<N, M>
where [(); N + 1]: Same<[(); M + 2]>
{ fn measure(&self) -> usize { 823 } }
impl<const N: usize, const M: usize> Right for Holder<N, M>
where [(); N + 2]: Same<[(); M + 3]>
{ fn measure(&self) -> usize { 827 } }
fn unresolved<const N: usize, const M: usize>() -> usize
where [(); N + 1]:, [(); M + 2]:, [(); N + 2]:, [(); M + 3]:
{ Holder::<N, M>.measure() }
fn main() { let _ = unresolved::<3, 2>(); }
