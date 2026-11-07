#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

const fn pointer_width<T: ?Sized>() -> usize {
    std::mem::size_of::<*const T>()
}

struct Packet<T: ?Sized, const N: usize = { pointer_width::<T>() }>(PhantomData<T>);

trait Stamp {
    fn stamp() -> usize;
}

impl<T: ?Sized, const N: usize> Stamp for Packet<T, N> {
    fn stamp() -> usize {
        N
    }
}

fn inspect<T>() -> usize
where
    [(); pointer_width::<dyn Iterator<Item = T> + Send>()]:,
{
    <Packet<dyn Iterator<Item = T> + Send> as Stamp>::stamp()
}

fn main() {
    assert_eq!(inspect::<u16>(), 2 * std::mem::size_of::<usize>());
}
