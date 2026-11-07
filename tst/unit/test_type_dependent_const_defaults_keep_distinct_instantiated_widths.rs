#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

struct Buffer<T, const N: usize = { std::mem::size_of::<T>() }> {
    bytes: [u8; N],
    marker: PhantomData<T>,
}

fn make<T>() -> Buffer<T>
where
    [(); std::mem::size_of::<T>()]:,
{
    Buffer {
        bytes: [41; std::mem::size_of::<T>()],
        marker: PhantomData,
    }
}

fn main() {
    let first = make::<u16>();
    let second = make::<[u32; 3]>();
    assert_eq!(first.bytes.len(), 2);
    assert_eq!(second.bytes.len(), 12);
    assert_eq!(first.bytes[1], 41);
    assert_eq!(second.bytes[11], 41);
}
