//@ compile-variant: -O
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Storage { type Bytes; }
trait First {
    fn choose<const M: usize>(&self) -> usize
    where Self: Storage<Bytes = [u8; M + 1]>, [(); M + 1]:;
}
trait Second {
    fn choose<const M: usize>(&self) -> usize
    where Self: Storage<Bytes = [u8; M + 1]>, [(); M + 1]:;
}
struct Buffer<const N: usize>;
impl<const N: usize> Storage for Buffer<N>
where [(); N + 1]:
{ type Bytes = [u8; N + 1]; }
impl<const N: usize> First for Buffer<N>
where [(); N + 1]:
{
    fn choose<const M: usize>(&self) -> usize
    where Self: Storage<Bytes = [u8; M + 1]>, [(); M + 1]:
    { M + 37 }
}
impl<const N: usize> Second for Buffer<N>
where [(); N + 1]:
{
    fn choose<const M: usize>(&self) -> usize
    where Self: Storage<Bytes = [u8; M + 1]>, [(); M + 1]:
    { M + 43 }
}
fn resolved<T, const N: usize>(value: &T) -> usize
where T: First + Second + Storage<Bytes = [u8; N + 1]>, [(); N + 1]:
{
    First::choose::<N>(value) + Second::choose::<N>(value)
}
fn main() { assert_eq!(resolved(&Buffer::<4>), 88); }
