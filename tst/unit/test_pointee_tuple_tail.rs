// A tuple is unsized when its last element is, and it takes that element's
// metadata.
//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(ptr_metadata)]

use std::ptr::{DynMetadata, Pointee};

trait Trait<U> {}
struct MyDst<T: ?Sized>(T);

fn meta_is<T: Pointee<Metadata = U> + ?Sized, U>() {}

pub fn works<T>() {
    meta_is::<T, ()>();
    meta_is::<[T], usize>();
    meta_is::<str, usize>();
    meta_is::<dyn Trait<T>, DynMetadata<dyn Trait<T>>>();
    meta_is::<MyDst<T>, ()>();
    meta_is::<(u8, u16), ()>();
    meta_is::<(u8, [u16]), usize>();
    meta_is::<((((([u8],),),),),), usize>();
}
