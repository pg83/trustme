#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn count(n: usize) -> usize { n + 1 }
struct Source<const N: usize>;
struct Buffer<const N: usize>([u8; N]);
trait Produce { type Output; fn produce(self) -> Self::Output; }
impl<const N: usize> Produce for Source<N> where [(); count(N)]: { type Output = *const [u8; count(N)]; fn produce(self) -> Self::Output { &[N as u8; count(N)] as *const _ } }
fn relay<const N: usize>(source: Source<N>) -> *const [u8; count(N)] where [(); count(N)]: { source.produce() }
fn main() { let value = relay(Source::<7>); assert_eq!(unsafe { *value }, [7u8; 8]); }
