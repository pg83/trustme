//@ compile-variant: -O
#![feature(generic_const_items)]
#![allow(incomplete_features)]

trait Widths: Sized {
    const WIDTH<T>: usize = core::mem::size_of::<T>() where T: Copy;
}
impl Widths for () {}

fn main() {
    assert_eq!(<() as Widths>::WIDTH::<u64>, 8);
    assert_eq!(<() as Widths>::WIDTH::<[u16; 3]>, 6);
}
