#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use core::mem::size_of;

struct Foo<T, const N: usize>(T);

impl<T> Foo<T, { size_of::<T>() }> {
    fn test() {
        let _: [u8; size_of::<T>()];
    }
}

fn main() {}
