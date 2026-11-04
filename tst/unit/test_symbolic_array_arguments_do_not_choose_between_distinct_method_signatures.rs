//@ compile-fail: error
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
struct Holder<const N: usize>;
trait Left<const N: usize> where [(); N + 1]: {
    fn measure(&self, bytes: [u8; N + 1]) -> usize;
}
trait Right<const N: usize> where [(); N + 2]: {
    fn measure(&self, bytes: [u8; N + 2]) -> usize;
}
impl<const N: usize> Left<N> for Holder<N> where [(); N + 1]: {
    fn measure(&self, bytes: [u8; N + 1]) -> usize { bytes.len() + 859 }
}
impl<const N: usize> Right<N> for Holder<N> where [(); N + 2]: {
    fn measure(&self, bytes: [u8; N + 2]) -> usize { bytes.len() + 863 }
}
fn unresolved<const N: usize, const M: usize>(value: Holder<N>, bytes: [u8; M + 2]) -> usize
where [(); N + 1]:, [(); N + 2]:, [(); M + 2]:
{ value.measure(bytes) }
fn main() { let _ = unresolved(Holder::<3>, [11u8; 4]); }
