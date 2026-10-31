#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn count(n: usize) -> usize { n + 1 }
struct Buffer<const N: usize>([u8; N]);
struct Source<const N: usize>;
trait Produce { type Output; fn produce(self) -> Self::Output; }
fn read<const N: usize>(value: Buffer<{ count(N) }>) -> u8 where [(); count(N)]: { value.0[0] }
impl<const N: usize> Produce for Source<N> where [(); count(N)]: {
    type Output = fn(Buffer<{ count(N) }>) -> u8;
    fn produce(self) -> Self::Output { read::<N> }
}
fn relay<const N: usize>(source: Source<N>) -> fn(Buffer<{ count(N) }>) -> u8
where [(); count(N)]: { source.produce() }
fn main() { let f = relay(Source::<3>); assert_eq!(f(Buffer([83u8; 4])), 83); }
