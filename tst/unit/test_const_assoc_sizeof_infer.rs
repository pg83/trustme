//@ crate-type: lib
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait HasSize {
    const SIZE: usize;
}

impl<T> HasSize for T {
    const SIZE: usize = core::mem::size_of::<T>();
}

struct Array<T: HasSize>([u8; T::SIZE])
where
    [(); T::SIZE]:;

fn check<T: HasSize>()
where
    [(); T::SIZE]:,
{
    let _: Array<T> = Array::<_>(make());
}

fn make() -> ! {
    panic!()
}

fn main() {}
