#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn count(n: usize) -> usize { n + 1 }
struct Source<const N: usize>;
struct Buffer<const N: usize>([u8; N]);
trait Produce { type Output; fn produce(self) -> Self::Output; }
impl<const N: usize> Produce for Source<N> where [(); count(N)]: { type Output = (Buffer<{ count(N) }>, [u16; count(N)]); fn produce(self) -> Self::Output { (Buffer([N as u8; count(N)]), [N as u16 + 10; count(N)]) } }
fn relay<const N: usize>(source: Source<N>) -> (Buffer<{ count(N) }>, [u16; count(N)]) where [(); count(N)]: { source.produce() }
fn main() { let value = relay(Source::<7>); assert_eq!(value.0.0, [7u8; 8]); assert_eq!(value.1, [17u16; 8]); }
