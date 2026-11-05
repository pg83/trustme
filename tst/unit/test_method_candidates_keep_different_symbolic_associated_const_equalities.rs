//@ compile-fail: error
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Storage { type Bytes; }
trait First {
    fn choose<const M: usize>(&self) -> usize
    where Self: Storage<Bytes = [u8; M + 1]>, [(); M + 1]:;
}
trait Second {
    fn choose<const M: usize>(&self) -> usize
    where Self: Storage<Bytes = [u8; M + 2]>, [(); M + 2]:;
}

fn unresolved<T, const N: usize, const M: usize>(value: &T) -> usize
where T: First + Second + Storage<Bytes = [u8; N + 1]>,
      [(); N + 1]:, [(); M + 1]:, [(); M + 2]:
{
    value.choose::<M>()
}

fn main() {}
